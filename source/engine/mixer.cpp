// mixer.cpp - see mixer.h.
#include "mixer.h"
#include "../platform/sys.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define STB_VORBIS_NO_PUSHDATA_API
#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"

namespace mixer {

// ---------------------------------------------------------------- streams
static const int STREAM_BUF = 4096;   // frames of decoded audio held per stream

struct Stream {
	stb_vorbis *v = nullptr;
	uint8_t *file = nullptr;
	int channels = 2, rate = 44100;
	float buf[STREAM_BUF * 2];   // stereo
	int frames = 0;              // valid frames in buf
	bool eof = false;
};

Stream *open_vorbis(uint8_t *file, uint32_t size) {
	int err = 0;
	stb_vorbis *v = stb_vorbis_open_memory(file, (int)size, &err, nullptr);
	if (!v) {
		free(file);
		sys::log("audio: bad Ogg Vorbis data (error %d)\n", err);
		return nullptr;
	}
	Stream *s = new Stream;
	s->v = v;
	s->file = file;
	stb_vorbis_info info = stb_vorbis_get_info(v);
	s->channels = info.channels;
	s->rate = (int)info.sample_rate;
	return s;
}

void close_stream(Stream *s) {
	if (!s) return;
	if (s->v) stb_vorbis_close(s->v);
	free(s->file);
	delete s;
}

// Decode into buf[from..], looping if asked. Returns the new frame count.
static int stream_fill(Stream *s, int from, bool loop) {
	int n = from;
	int guard = 0;
	while (n < STREAM_BUF && !s->eof) {
		int got = stb_vorbis_get_samples_float_interleaved(s->v, 2, s->buf + n * 2, (STREAM_BUF - n) * 2);
		if (got > 0) {
			if (s->channels == 1)
				for (int i = n; i < n + got; i++) s->buf[i * 2 + 1] = s->buf[i * 2];
			n += got;
			guard = 0;
		} else if (loop && ++guard < 2) {
			stb_vorbis_seek_start(s->v);
		} else {
			s->eof = true;
		}
	}
	return n;
}

// ---------------------------------------------------------------- voices (audio thread)
struct Voice {
	bool active = false, loop = false, paused = false;
	float vol = 1, pitch = 1;
	const Pcm *pcm = nullptr;
	Stream *stream = nullptr;
	double pos = 0;    // frame position (PCM: in the buffer; stream: in s->buf)
	uint32_t gen = 0;
};
static Voice voices[MAX_VOICES];
static int g_rate = 48000;

// ---------------------------------------------------------------- command queue
enum Op : uint8_t { OP_PLAY_PCM, OP_PLAY_STREAM, OP_STOP, OP_VOL, OP_PITCH, OP_PAUSE };
struct Cmd {
	Op op;
	uint8_t voice;
	bool flag;
	float a, b;
	const Pcm *pcm;
	Stream *stream;
	uint32_t gen;
};
static const int QSIZE = 512;
static Cmd queue[QSIZE];
static volatile uint32_t q_head, q_tail;   // head: written by game thread; tail: by audio thread

// Generation counters make is_playing() exact without locks: the game
// thread bumps play_gen on each play; the audio thread publishes done_gen
// when that play ends.
static volatile uint32_t play_gen[MAX_VOICES], done_gen[MAX_VOICES];
static volatile int g_active;
static volatile float g_load;

static void push(const Cmd &c) {
	uint32_t h = q_head;
	uint32_t next = (h + 1) % QSIZE;
	if (next == __atomic_load_n(&q_tail, __ATOMIC_ACQUIRE)) {
		static bool warned;
		if (!warned) { warned = true; sys::log("audio: command queue full, command dropped\n"); }
		if (c.op == OP_PLAY_STREAM) close_stream(c.stream);
		return;
	}
	queue[h] = c;
	__atomic_store_n(&q_head, next, __ATOMIC_RELEASE);
}

bool init(int outputRate) {
	g_rate = outputRate;
	return true;
}

int output_rate() { return g_rate; }

void play_pcm(int v, const Pcm *pcm, float vol, float pitch, bool loop) {
	if (v < 0 || v >= MAX_VOICES || !pcm || !pcm->frames) return;
	uint32_t g = play_gen[v] + 1;
	play_gen[v] = g;
	push({ OP_PLAY_PCM, (uint8_t)v, loop, vol, pitch, pcm, nullptr, g });
}

void play_stream(int v, Stream *s, float vol, float pitch, bool loop) {
	if (v < 0 || v >= MAX_VOICES || !s) { close_stream(s); return; }
	uint32_t g = play_gen[v] + 1;
	play_gen[v] = g;
	push({ OP_PLAY_STREAM, (uint8_t)v, loop, vol, pitch, nullptr, s, g });
}

void stop(int v) {
	if (v < 0 || v >= MAX_VOICES) return;
	__atomic_store_n(&done_gen[v], play_gen[v], __ATOMIC_RELEASE);
	push({ OP_STOP, (uint8_t)v, false, 0, 0, nullptr, nullptr, play_gen[v] });
}

void set_volume(int v, float vol) { if (v >= 0 && v < MAX_VOICES) push({ OP_VOL, (uint8_t)v, false, vol, 0, nullptr, nullptr, 0 }); }
void set_pitch(int v, float p) { if (v >= 0 && v < MAX_VOICES) push({ OP_PITCH, (uint8_t)v, false, p, 0, nullptr, nullptr, 0 }); }
void set_paused(int v, bool p) { if (v >= 0 && v < MAX_VOICES) push({ OP_PAUSE, (uint8_t)v, p, 0, 0, nullptr, nullptr, 0 }); }

bool is_playing(int v) {
	if (v < 0 || v >= MAX_VOICES) return false;
	return __atomic_load_n(&done_gen[v], __ATOMIC_ACQUIRE) != play_gen[v];
}

int active_voices() { return g_active; }
float render_load() { return g_load; }

// ---------------------------------------------------------------- audio thread
static void end_voice(Voice &vc, int index) {
	if (vc.stream) close_stream(vc.stream);
	vc.stream = nullptr;
	vc.pcm = nullptr;
	if (vc.active) {
		// Only publish if no newer play was requested meanwhile.
		uint32_t d = __atomic_load_n(&done_gen[index], __ATOMIC_ACQUIRE);
		if ((int32_t)(vc.gen - d) > 0) __atomic_store_n(&done_gen[index], vc.gen, __ATOMIC_RELEASE);
	}
	vc.active = false;
}

static void apply_commands() {
	uint32_t t = q_tail;
	uint32_t h = __atomic_load_n(&q_head, __ATOMIC_ACQUIRE);
	while (t != h) {
		const Cmd &c = queue[t];
		Voice &vc = voices[c.voice];
		switch (c.op) {
		case OP_PLAY_PCM:
		case OP_PLAY_STREAM:
			if (vc.stream) close_stream(vc.stream);
			vc = Voice();
			vc.active = true;
			vc.loop = c.flag;
			vc.vol = c.a;
			vc.pitch = c.b;
			vc.pcm = c.pcm;
			vc.stream = c.stream;
			vc.gen = c.gen;
			if (vc.stream) vc.stream->frames = stream_fill(vc.stream, 0, vc.loop);
			break;
		case OP_STOP:
			end_voice(vc, c.voice);
			break;
		case OP_VOL: vc.vol = c.a; break;
		case OP_PITCH: vc.pitch = c.a; break;
		case OP_PAUSE: vc.paused = c.flag; break;
		}
		t = (t + 1) % QSIZE;
	}
	__atomic_store_n(&q_tail, t, __ATOMIC_RELEASE);
}

static inline float s16(int16_t v) { return v * (1.0f / 32768.0f); }

static void mix_pcm(Voice &vc, int index, float *out, int frames) {
	const Pcm &p = *vc.pcm;
	double step = (double)p.rate / g_rate * (vc.pitch > 0.01f ? vc.pitch : 0.01f);
	const uint32_t n = p.frames;
	for (int i = 0; i < frames; i++) {
		uint32_t i0 = (uint32_t)vc.pos;
		if (i0 >= n) {
			if (!vc.loop) { end_voice(vc, index); return; }
			vc.pos -= n;
			i0 = (uint32_t)vc.pos;
			if (i0 >= n) { vc.pos = 0; i0 = 0; }
		}
		uint32_t i1 = i0 + 1 < n ? i0 + 1 : (vc.loop ? 0 : i0);
		float f = (float)(vc.pos - i0);
		float l, r;
		if (p.channels == 2) {
			float l0 = s16(p.data[i0 * 2]), r0 = s16(p.data[i0 * 2 + 1]);
			float l1 = s16(p.data[i1 * 2]), r1 = s16(p.data[i1 * 2 + 1]);
			l = l0 + (l1 - l0) * f;
			r = r0 + (r1 - r0) * f;
		} else {
			float a = s16(p.data[i0]), b = s16(p.data[i1]);
			l = r = a + (b - a) * f;
		}
		out[i * 2] += l * vc.vol;
		out[i * 2 + 1] += r * vc.vol;
		vc.pos += step;
	}
}

static void mix_stream(Voice &vc, int index, float *out, int frames) {
	Stream &s = *vc.stream;
	double step = (double)s.rate / g_rate * (vc.pitch > 0.01f ? vc.pitch : 0.01f);
	for (int i = 0; i < frames; i++) {
		int i0 = (int)vc.pos;
		if (i0 + 1 >= s.frames) {
			// Keep the last frame (interpolation partner), refill after it.
			if (s.frames > 0 && i0 < s.frames) {
				s.buf[0] = s.buf[(s.frames - 1) * 2];
				s.buf[1] = s.buf[(s.frames - 1) * 2 + 1];
				vc.pos -= (s.frames - 1);
				s.frames = stream_fill(&s, 1, vc.loop);
			} else {
				vc.pos = 0;
				s.frames = stream_fill(&s, 0, vc.loop);
			}
			i0 = (int)vc.pos;
			if (i0 + 1 >= s.frames) { end_voice(vc, index); return; }
		}
		float f = (float)(vc.pos - i0);
		const float *a = s.buf + i0 * 2;
		out[i * 2] += (a[0] + (a[2] - a[0]) * f) * vc.vol;
		out[i * 2 + 1] += (a[1] + (a[3] - a[1]) * f) * vc.vol;
		vc.pos += step;
	}
}

void render(float *out, int frames) {
	uint64_t t0 = sys::time_us();
	apply_commands();
	memset(out, 0, sizeof(float) * 2 * frames);
	int active = 0;
	for (int v = 0; v < MAX_VOICES; v++) {
		Voice &vc = voices[v];
		if (!vc.active) continue;
		active++;
		if (vc.paused || vc.vol <= 0.0f) {
			// Silent voices still advance? FMOD pauses keep position; volume 0 keeps playing.
			if (vc.paused) continue;
		}
		if (vc.pcm) mix_pcm(vc, v, out, frames);
		else if (vc.stream) mix_stream(vc, v, out, frames);
	}
	for (int i = 0; i < frames * 2; i++) {   // soft clip protection
		float x = out[i];
		out[i] = x > 1.0f ? 1.0f : (x < -1.0f ? -1.0f : x);
	}
	g_active = active;
	double budget = frames * 1e6 / g_rate;
	float load = (float)((sys::time_us() - t0) / budget);
	g_load = g_load * 0.95f + load * 0.05f;
}

} // namespace mixer

// The decoder implementation lives in this file only.
#undef STB_VORBIS_HEADER_ONLY
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#pragma GCC diagnostic ignored "-Wunused-value"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wtype-limits"
#pragma GCC diagnostic ignored "-Wmisleading-indentation"
#include "stb_vorbis.c"
#pragma GCC diagnostic pop
