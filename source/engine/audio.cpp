#include "audio.h"
#include "../platform/sys.h"

namespace audio {

static bool verbose = false;
void set_verbose(bool on) { verbose = on; }

#define TRACE(...) do { if (verbose) sys::log(__VA_ARGS__); } while (0)

void play_sound(const char *name, float vol, float pitch) { TRACE("audio: se %s (%.2f, %.2f)\n", name, vol, pitch); }
void play_song(const char *name, float, float vol, float) { TRACE("audio: bgm %s (%.2f)\n", name, vol); }
void queue_song(const char *name, float, float vol, float) { TRACE("audio: queue bgm %s (%.2f)\n", name, vol); }
void play_bgs(const char *name, float vol, float) { TRACE("audio: bgs %s (%.2f)\n", name, vol); }
void play_me(const char *name, float vol, float) { TRACE("audio: me %s (%.2f)\n", name, vol); }
void fade_out_bgm(float s) { TRACE("audio: fade bgm %.2fs\n", s); }
void fade_out_bgs(float s) { TRACE("audio: fade bgs %.2fs\n", s); }
void memorize_bgm_bgs() {}
void restore_bgm_bgs() {}

} // namespace audio
