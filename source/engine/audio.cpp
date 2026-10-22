// audio.cpp - SoundManager + FmodManager logic from the PC version, on top
// of the mixer. Runs on the game thread (update() once per frame, 60 Hz,
// same as the C#'s fade timers); the platform backend runs the mixer on
// its own audio thread.
#include "audio.h"
#include <stdio.h>
#include "mixer.h"
#include "util.h"
#include "../platform/audio_out.h"
#include "../platform/sys.h"

#include <stdlib.h>
#include <string.h>
#include <string>
#include <unordered_map>
#include <vector>

namespace audio {

enum { V_BGM = 0, V_BGS = 1, V_ME = 2, V_SFX0 = 3 };

static bool g_ok;
static bool verbose;
static int bgm_vol_setting = 100, sfx_vol_setting = 100;

// ---------------------------------------------------------------- sound effects (WAV, in memory)
static std::unordered_map<std::string, mixer::Pcm *> sfx;
static std::unordered_map<std::string, bool> sfx_missing;

static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t le32(const uint8_t *p) { return (uint32_t)(p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24); }

static mixer::Pcm *parse_wav(const uint8_t *d, size_t n, const char *name) {
	if (n < 12 || memcmp(d, "RIFF", 4) || memcmp(d + 8, "WAVE", 4)) return nullptr;
	int fmt = 0, ch = 0, rate = 0, bits = 0;
	const uint8_t *data = nullptr;
	uint32_t data_len = 0;
	size_t p = 12;
	while (p + 8 <= n) {
		uint32_t len = le32(d + p + 4);
		const uint8_t *body = d + p + 8;
		if (p + 8 + len > n) len = (uint32_t)(n - p - 8);
		if (!memcmp(d + p, "fmt ", 4) && len >= 16) {
			fmt = le16(body);
			ch = le16(body + 2);
			rate = (int)le32(body + 4);
			bits = le16(body + 14);
			if (fmt == 0xFFFE && len >= 26) fmt = le16(body + 24);   // WAVE_FORMAT_EXTENSIBLE sub-format
		} else if (!memcmp(d + p, "data", 4)) {
			data = body;
			data_len = len;
		}
		p += 8 + len + (len & 1);
	}
	if (!data || (ch != 1 && ch != 2) || rate <= 0) return nullptr;
	int bps = bits / 8;
	if (bps <= 0 || (fmt != 1 && fmt != 3)) {
		sys::log("audio: %s: unsupported WAV format %d/%d-bit\n", name, fmt, bits);
		return nullptr;
	}
	uint32_t frames = data_len / (bps * ch);
	auto *pcm = new mixer::Pcm;
	pcm->channels = ch;
	pcm->rate = rate;
	pcm->frames = frames;
	pcm->data = (int16_t *)malloc(sizeof(int16_t) * frames * ch);
	for (uint32_t i = 0; i < frames * ch; i++) {
		const uint8_t *s = data + i * bps;
		int v;
		if (fmt == 3 && bits == 32) {
			uint32_t u = le32(s);
			float f;
			memcpy(&f, &u, 4);
			v = (int)(f * 32767.0f);
		} else if (bits == 8) v = ((int)s[0] - 128) << 8;
		else if (bits == 16) v = (int16_t)le16(s);
		else if (bits == 24) v = (int32_t)((uint32_t)s[0] << 8 | (uint32_t)s[1] << 16 | (uint32_t)s[2] << 24) >> 16;
		else v = (int32_t)le32(s) >> 16;
		pcm->data[i] = (int16_t)(v > 32767 ? 32767 : (v < -32768 ? -32768 : v));
	}
	return pcm;
}

static mixer::Pcm *get_sfx(const std::string &name) {
	std::string key = util::lower(name);
	auto it = sfx.find(key);
	if (it != sfx.end()) return it->second;
	if (sfx_missing.count(key)) return nullptr;
	uint8_t *d;
	size_t n;
	mixer::Pcm *pcm = nullptr;
	if (sys::read_file(("gamedata/sfx/" + key + ".wav").c_str(), &d, &n)) {
		pcm = parse_wav(d, n, key.c_str());
		free(d);
	}
	if (!pcm) {
		sfx_missing[key] = true;
		sys::log("audio: sound effect '%s' missing or unreadable\n", key.c_str());
		return nullptr;
	}
	sfx[key] = pcm;
	return pcm;
}

static mixer::Stream *open_ogg(const char *dir, const std::string &name) {
	std::string rel = std::string("gamedata/") + dir + "/" + name + ".ogg";
	uint8_t *d;
	size_t n;
	if (!sys::read_file(rel.c_str(), &d, &n)) {
		sys::log("audio: missing %s\n", rel.c_str());
		return nullptr;
	}
	uint64_t t0 = sys::time_us();
	mixer::Stream *s = mixer::open_vorbis(d, (uint32_t)n);   // takes ownership of d
	sys::log("audio: opened %s (%.1f KB) in %.1f ms\n", rel.c_str(), n / 1024.0, (sys::time_us() - t0) / 1000.0);
	return s;
}

// ---------------------------------------------------------------- FmodManager state
static std::string currentSong;           // FmodManager.CurrentSong
static float currentSongVolume = 0;
static bool bgmPlaying;                   // bgmChannel.hasHandle()
static float bgmPitch = 1;
static float fadeVol = 1, fadeStartVol = 0, fadeEndVol = 1;
static int fadeTimer = 0, fadeTimeTotal = 0;
static std::string queuedSongName;
static float queuedSongVol = 1, queuedSongPitch = 1, queuedSongFadeIn = 0;
static bool meActive;                     // meChannel.hasHandle()
static std::string currentBGSTitle;
static float currentBGSVolume = 1;
static bool bgsActive;
static int bgsFadeOutTimer = 0, bgsFadeOutTimeTotal = 0;
static int nextSfxVoice = V_SFX0;

// SoundManager state
static std::string smSongName, smBgsName, memSongName, memBgsName;
static float smSongVol = 1, smSongPitch = 1, smBgsVol = 1, smBgsPitch = 1;
static float memSongVol = 1, memSongPitch = 1, memBgsVol = 1, memBgsPitch = 1;

static float bgm_scale() { return bgm_vol_setting / 100.0f; }
static float sfx_scale() { return sfx_vol_setting / 100.0f; }

static void fm_stop_song() {
	currentSong.clear();
	currentSongVolume = 0;
	if (bgmPlaying) mixer::stop(V_BGM);
	bgmPlaying = false;
}

static void fm_stop_bgs() {
	if (bgsActive) mixer::stop(V_BGS);
	bgsActive = false;
	bgsFadeOutTimeTotal = 0;
	bgsFadeOutTimer = 0;
}

static void fm_stop_me() {
	if (meActive) mixer::stop(V_ME);
	meActive = false;
}

static void fm_play_song(const std::string &title, float fadeIn, float volume, float pitch) {
	if (title.empty()) { fm_stop_song(); return; }
	if (title != currentSong || !bgmPlaying) {
		fm_stop_song();
		currentSong = title;
		mixer::Stream *s = open_ogg("music", title);
		if (s) {
			mixer::play_stream(V_BGM, s, 0.0f, pitch, true);
			bgmPlaying = true;
		}
	}
	bgmPitch = pitch;
	mixer::set_pitch(V_BGM, pitch);
	fadeTimer = 0;
	currentSongVolume = volume;
	if (fadeIn <= 0.0f) {
		fadeVol = 1;
		fadeTimeTotal = 0;
		fadeStartVol = 1;
		fadeEndVol = 1;
	} else {
		fadeVol = 0;
		fadeTimeTotal = (int)(fadeIn * 60.0f);
		fadeStartVol = 0;
		fadeEndVol = 1;
	}
	bool mePlaying = meActive && mixer::is_playing(V_ME);
	mixer::set_volume(V_BGM, mePlaying ? 0.0f : currentSongVolume * fadeVol * bgm_scale());
	mixer::set_paused(V_BGM, false);
}

static void fm_queue_song(const std::string &title, float fadeIn = 0, float vol = 1, float pitch = 1) {
	queuedSongName = title;
	queuedSongFadeIn = fadeIn;
	queuedSongVol = vol;
	queuedSongPitch = pitch;
}

static void fm_dequeue_song() {
	std::string n = queuedSongName;
	fm_play_song(n, queuedSongFadeIn, queuedSongVol, queuedSongPitch);
	queuedSongName.clear();
}

static void fm_fade_out_bgm(float t) {
	fadeTimer = 0;
	fadeTimeTotal = (int)(t * 60.0f);
	fadeStartVol = fadeVol;
	fadeEndVol = 0;
}

// ---------------------------------------------------------------- public API
void set_verbose(bool on) { verbose = on; }
#define TRACE(...) do { if (verbose) sys::log(__VA_ARGS__); } while (0)

bool init() {
	mixer::init(48000);
	g_ok = audio_out::start(48000);
	sys::log("audio: %s\n", g_ok ? "output started (48 kHz stereo)" : "output FAILED to start - running silent");
	return g_ok;
}

void shutdown() { audio_out::stop(); }

void preload_sfx() {
	// The PC version loads every sfx/*.wav at boot; we do the same so the
	// first use of a sound never hitches.
	uint64_t t0 = sys::time_us();
	std::vector<std::string> names;
	sys::list_dir("gamedata/sfx", names);
	size_t bytes = 0;
	int n = 0;
	for (auto &f : names) {
		std::string lf = util::lower(f);
		if (lf.size() < 5 || lf.compare(lf.size() - 4, 4, ".wav") != 0) continue;
		mixer::Pcm *p = get_sfx(lf.substr(0, lf.size() - 4));
		if (p) { bytes += p->frames * p->channels * 2; n++; }
	}
	sys::log("audio: %d sound effects loaded (%.1f MB) in %.0f ms\n", n, bytes / 1048576.0, (sys::time_us() - t0) / 1000.0);
}

void play_sound(const char *name, float vol, float pitch) {
	TRACE("audio: se %s (%.2f, %.2f)\n", name, vol, pitch);
	if (!name || !*name) return;
	mixer::Pcm *p = get_sfx(name);
	if (!p) return;
	// Find a free effect voice; if all are busy, reuse the next in turn.
	int v = -1;
	for (int i = 0; i < mixer::MAX_VOICES - V_SFX0; i++) {
		int c = V_SFX0 + (nextSfxVoice - V_SFX0 + i) % (mixer::MAX_VOICES - V_SFX0);
		if (!mixer::is_playing(c)) { v = c; break; }
	}
	if (v < 0) v = nextSfxVoice;
	nextSfxVoice = V_SFX0 + (v - V_SFX0 + 1) % (mixer::MAX_VOICES - V_SFX0);
	mixer::play_pcm(v, p, vol * sfx_scale(), pitch, false);
}

void play_song(const char *name, float fadeIn, float vol, float pitch) {
	TRACE("audio: bgm %s (%.2f)\n", name, vol);
	smSongName = name;
	smSongVol = vol;
	smSongPitch = pitch;
	if (smSongName.empty()) { fm_stop_song(); return; }
	fm_play_song(smSongName, fadeIn, vol, pitch);
	fm_queue_song("");
}

void queue_song(const char *name, float fadeIn, float vol, float pitch) {
	TRACE("audio: queue bgm %s (%.2f)\n", name, vol);
	smSongName = name;
	smSongVol = vol;
	smSongPitch = pitch;
	if (smSongName == currentSong) {
		fm_play_song(smSongName, 0, vol, pitch);
		fm_queue_song("");
	} else {
		fm_fade_out_bgm(0.5f);
		fm_queue_song(smSongName, fadeIn, vol, pitch);
	}
}

void play_bgs(const char *name, float vol, float pitch) {
	TRACE("audio: bgs %s (%.2f)\n", name, vol);
	stop_bgs();
	smBgsName = name;
	smBgsVol = vol;
	smBgsPitch = pitch;
	if (smBgsName.empty()) return;
	std::string key = util::lower(smBgsName);
	if (key != currentBGSTitle || !bgsActive) {
		fm_stop_bgs();
		currentBGSTitle = key;
		if (mixer::Pcm *p = get_sfx(key)) {
			mixer::play_pcm(V_BGS, p, vol * sfx_scale(), pitch, true);
			bgsActive = true;
		}
	}
	currentBGSVolume = vol;
}

void stop_bgs() {
	fm_stop_bgs();
	smBgsName.clear();
	smBgsPitch = 0;
	smBgsVol = 1;
}

void fade_out_bgs(float seconds) {
	TRACE("audio: fade bgs %.2fs\n", seconds);
	bgsFadeOutTimer = 0;
	bgsFadeOutTimeTotal = (int)(seconds * 60.0f);
	smBgsName.clear();
	smBgsPitch = 0;
	smBgsVol = 1;
}

void play_me(const char *name, float vol, float pitch) {
	TRACE("audio: me %s (%.2f)\n", name, vol);
	fm_stop_me();
	if (bgmPlaying) mixer::set_volume(V_BGM, 0.0f);
	mixer::Stream *s = open_ogg("music_effects", name);
	if (!s) return;
	mixer::play_stream(V_ME, s, vol * sfx_scale(), pitch, false);
	meActive = true;
}

void fade_out_bgm(float seconds) {
	TRACE("audio: fade bgm %.2fs\n", seconds);
	fm_fade_out_bgm(seconds);
}

void fade_in_bgm(float targetVolume, float seconds) {
	currentSongVolume = targetVolume;
	fadeStartVol = 0;
	fadeEndVol = targetVolume;
	fadeTimer = 0;
	fadeTimeTotal = (int)(seconds * 60.0f);
}

void set_song_paused(bool paused) {
	if (bgmPlaying) mixer::set_paused(V_BGM, paused);
}

void memorize_bgm_bgs() {
	memSongName = smSongName; memSongPitch = smSongPitch; memSongVol = smSongVol;
	memBgsName = smBgsName; memBgsPitch = smBgsPitch; memBgsVol = smBgsVol;
}

void restore_bgm_bgs() {
	std::string s = memSongName, b = memBgsName;
	play_song(s.c_str(), 0, memSongVol, memSongPitch);
	play_bgs(b.c_str(), memBgsVol, memBgsPitch);
}

void update() {
	// Heartbeat for the log: proves the output thread is mixing.
	static unsigned frames, lastMixed;
	if (++frames % 1800 == 0) {   // every ~30 s
		unsigned m = audio_out::mixed_blocks();
		sys::log("audio: %u blocks mixed in the last 30 s, %d voices, mix load %.0f%%\n", m - lastMixed,
		         mixer::active_voices(), mixer::render_load() * 100.0f);
		lastMixed = m;
	}
	// FmodManager.Update
	if (fadeTimer < fadeTimeTotal && fadeTimeTotal > 0) {
		fadeTimer++;
		float r = (float)fadeTimer / (float)fadeTimeTotal;
		fadeVol = fadeStartVol + (fadeEndVol - fadeStartVol) * r;
	} else {
		fadeVol = fadeEndVol;
		if (fadeVol <= 0.0f) {
			fm_stop_song();
			if (!queuedSongName.empty()) fm_dequeue_song();
		}
	}
	if (meActive) {
		if (!mixer::is_playing(V_ME)) {
			fm_stop_me();
			if (bgmPlaying) {
				if (!queuedSongName.empty()) fm_dequeue_song();
				fadeVol = 0;
				fadeTimer = 0;
				fadeTimeTotal = 60;
				fadeStartVol = 0;
				fadeEndVol = 1;
				mixer::set_volume(V_BGM, 0.0f);
			}
		} else if (bgmPlaying) {
			mixer::set_volume(V_BGM, 0.0f);
		}
	} else if (bgmPlaying) {
		mixer::set_volume(V_BGM, currentSongVolume * fadeVol * bgm_scale());
	}
	if (bgsActive) {
		if (bgsFadeOutTimeTotal > 0) {
			bgsFadeOutTimer++;
			if (bgsFadeOutTimer >= bgsFadeOutTimeTotal) fm_stop_bgs();
			else
				mixer::set_volume(V_BGS, currentBGSVolume * ((float)(bgsFadeOutTimeTotal - bgsFadeOutTimer) / bgsFadeOutTimeTotal) *
				                             sfx_scale());
		} else {
			mixer::set_volume(V_BGS, currentBGSVolume * sfx_scale());
		}
	}
	audio_out::pump();   // host backend renders here; PS3 runs its own thread
}

SaveData get_save_data() {
	SaveData d;
	snprintf(d.bgm, sizeof(d.bgm), "%s", currentSong.c_str());
	d.bgmVol = currentSongVolume;
	d.bgmPitch = bgmPitch;
	snprintf(d.bgs, sizeof(d.bgs), "%s", smBgsName.c_str());
	d.bgsVol = smBgsVol;
	d.bgsPitch = smBgsPitch;
	return d;
}

void load_save_data(const SaveData &d) {
	play_song(d.bgm, 0.0f, d.bgmVol, d.bgmPitch);
	play_bgs(d.bgs, d.bgsVol, d.bgsPitch);
}

void stop_all() {
	fm_stop_song();
	fm_stop_bgs();
	fm_stop_me();
	for (int v = V_SFX0; v < mixer::MAX_VOICES; v++) mixer::stop(v);
	queuedSongName.clear();
	smSongName.clear();
	smBgsName.clear();
	memSongName.clear();
	memBgsName.clear();
	fadeTimeTotal = 0;
	fadeVol = 1;
}

void set_volumes(int bgm, int sfxv) { bgm_vol_setting = bgm; sfx_vol_setting = sfxv; }
const char *current_song() { return currentSong.c_str(); }
bool output_ok() { return g_ok; }

} // namespace audio
