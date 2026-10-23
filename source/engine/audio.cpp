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
#include "../platform/loader.h"

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

// Music effects (get_item etc.) are small: their files are read in the
// background at boot and kept, so playing one never touches the disk.
struct MeFile { sys::LoadJob *job = nullptr; uint8_t *data = nullptr; size_t size = 0; };
static std::unordered_map<std::string, MeFile> me_files;   // lower-case name

static mixer::Stream *open_me(const std::string &name) {
	auto it = me_files.find(util::lower(name));
	if (it == me_files.end()) return open_ogg("music_effects", name);
	MeFile &f = it->second;
	if (f.job) {
		if (!sys::load_done(f.job)) {   // still queued: read it now instead
			sys::load_cancel(f.job);
			f.job = nullptr;
			return open_ogg("music_effects", name);
		}
		if (!sys::load_take(f.job, &f.data, &f.size)) f.data = nullptr;
		f.job = nullptr;
	}
	if (!f.data) return open_ogg("music_effects", name);
	uint8_t *copy = (uint8_t *)malloc(f.size);   // the stream takes ownership of its copy
	if (!copy) return nullptr;
	memcpy(copy, f.data, f.size);
	return mixer::open_vorbis(copy, (uint32_t)f.size);
}

// Background music is read on the loader thread and starts playing as soon
// as the file is in memory (a frame or two later).
static sys::LoadJob *bgmJob;
static std::string bgmJobName;
static uint64_t bgmJobStart;
static bool bgmPaused;

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
	if (bgmJob) { sys::load_cancel(bgmJob); bgmJob = nullptr; }
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
		bgmJobName = "gamedata/music/" + title + ".ogg";
		bgmJob = sys::load_async(bgmJobName.c_str());
		bgmJobStart = sys::time_us();
		bgmPlaying = true;   // the stream is attached when the file arrives
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
	bgmPaused = false;
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

// ------------------------------------------------------------------ boot loading
// The boot console lists the sound effects and loads them one per frame
// (SoundManager.LoadSoundList / LoadNextSound). The files are read on the
// loader thread as soon as they are listed, so the console only waits when
// the hard drive is behind.
struct PendingSfx { std::string name; sys::LoadJob *job; };
static std::vector<PendingSfx> sfxQueue;
static size_t sfxQueueNext;
static int sfxCount = -1;
static uint64_t sfxLoadStart;
static size_t sfxBytes;

static void queue_music_effects() {
	static bool done;
	if (done) return;
	done = true;
	std::vector<std::string> me;
	sys::list_dir("gamedata/music_effects", me);
	for (auto &f : me) {
		std::string lf = util::lower(f);
		if (lf.size() < 5 || lf.compare(lf.size() - 4, 4, ".ogg") != 0) continue;
		me_files[lf.substr(0, lf.size() - 4)].job = sys::load_async(("gamedata/music_effects/" + f).c_str());
	}
}

int scan_sounds() {
	for (size_t i = sfxQueueNext; i < sfxQueue.size(); i++) sys::load_cancel(sfxQueue[i].job);
	sfxQueue.clear();
	sfxQueueNext = 0;
	std::vector<std::string> names;
	sys::list_dir("gamedata/sfx", names);
	for (auto &f : names) {
		std::string lf = util::lower(f);
		if (lf.find(".wav") == std::string::npos) continue;   // LoadSoundList: Contains(".wav")
		sfxQueue.push_back({ f, sys::load_async(("gamedata/sfx/" + f).c_str()) });
	}
	sfxCount = (int)sfxQueue.size();
	sfxLoadStart = sys::time_us();
	sfxBytes = 0;
	queue_music_effects();
	return sfxCount;
}

int sound_count() { return sfxCount < 0 ? 0 : sfxCount; }
bool has_more_sounds() { return sfxQueueNext < sfxQueue.size(); }
const char *next_sound_name() { return has_more_sounds() ? sfxQueue[sfxQueueNext].name.c_str() : ""; }

static void finish_sound(PendingSfx &p, bool wait) {
	std::string lf = util::lower(p.name);
	std::string key = lf.size() > 4 ? lf.substr(0, lf.size() - 4) : lf;
	if (wait) sys::load_wait(p.job);
	uint8_t *d = nullptr;
	size_t n = 0;
	bool ok = sys::load_take(p.job, &d, &n);
	p.job = nullptr;
	if (sfx.count(key)) { free(d); return; }   // already loaded on demand
	mixer::Pcm *pcm = ok ? parse_wav(d, n, key.c_str()) : nullptr;
	free(d);
	if (!pcm) {
		sfx_missing[key] = true;
		sys::log("audio: sound effect '%s' missing or unreadable\n", key.c_str());
		return;
	}
	sfx[key] = pcm;
	sfxBytes += pcm->frames * pcm->channels * 2;
}

bool load_next_sound() {
	if (!has_more_sounds()) return true;
	PendingSfx &p = sfxQueue[sfxQueueNext];
	if (!sys::load_done(p.job)) return false;   // still on its way from the hard drive
	finish_sound(p, false);
	sfxQueueNext++;
	if (!has_more_sounds())
		sys::log("audio: %d sound effects loaded (%.1f MB) in %.0f ms\n", sfxCount, sfxBytes / 1048576.0,
		         (sys::time_us() - sfxLoadStart) / 1000.0);
	return true;
}

void load_all_sounds() {
	if (sfxCount < 0) scan_sounds();
	bool any = has_more_sounds();
	while (has_more_sounds()) finish_sound(sfxQueue[sfxQueueNext++], true);
	if (any)
		sys::log("audio: %d sound effects loaded (%.1f MB) in %.0f ms\n", sfxCount, sfxBytes / 1048576.0,
		         (sys::time_us() - sfxLoadStart) / 1000.0);
}

void preload_sfx() {
	// Debug starts skip the boot console: load everything now.
	if (sfxCount < 0) scan_sounds();
	load_all_sounds();
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

static void (*g_songHook)(const char *);
void set_song_hook(void (*hook)(const char *)) { g_songHook = hook; }

void play_song(const char *name, float fadeIn, float vol, float pitch) {
	TRACE("audio: bgm %s (%.2f)\n", name, vol);
	if (g_songHook && name && *name) g_songHook(name);
	smSongName = name;
	smSongVol = vol;
	smSongPitch = pitch;
	if (smSongName.empty()) { fm_stop_song(); return; }
	fm_play_song(smSongName, fadeIn, vol, pitch);
	fm_queue_song("");
}

void queue_song(const char *name, float fadeIn, float vol, float pitch) {
	TRACE("audio: queue bgm %s (%.2f)\n", name, vol);
	if (g_songHook && name && *name) g_songHook(name);
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
	mixer::Stream *s = open_me(name);
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
	bgmPaused = paused;
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

static void poll_bgm_load() {
	if (!bgmJob || !sys::load_done(bgmJob)) return;
	sys::LoadJob *j = bgmJob;
	bgmJob = nullptr;
	uint8_t *d;
	size_t n;
	if (!sys::load_take(j, &d, &n)) {
		sys::log("audio: missing %s\n", bgmJobName.c_str());
		bgmPlaying = false;
		return;
	}
	mixer::Stream *s = mixer::open_vorbis(d, (uint32_t)n);   // takes ownership of d
	if (!s) { bgmPlaying = false; return; }
	mixer::play_stream(V_BGM, s, 0.0f, bgmPitch, true);
	bool mePlaying = meActive && mixer::is_playing(V_ME);
	mixer::set_volume(V_BGM, mePlaying ? 0.0f : currentSongVolume * fadeVol * bgm_scale());
	mixer::set_paused(V_BGM, bgmPaused);
	sys::log("audio: %s (%.0f KB) playing %.0f ms after it was asked for\n", bgmJobName.c_str(), n / 1024.0,
	         (sys::time_us() - bgmJobStart) / 1000.0);
}

void update() {
	poll_bgm_load();
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
