// audio.h - SoundManager interface. Playback is not implemented yet (the
// Ogg/SPU mixer is a later milestone); calls are accepted and, in verbose
// mode, logged so event scripts can be traced.
#pragma once

namespace audio {
void play_sound(const char *name, float vol = 1.0f, float pitch = 1.0f);
void play_song(const char *name, float fadeIn, float vol, float pitch);
void queue_song(const char *name, float fadeIn, float vol, float pitch);
void play_bgs(const char *name, float vol, float pitch);
void play_me(const char *name, float vol, float pitch);
void fade_out_bgm(float seconds);
void fade_out_bgs(float seconds);
void memorize_bgm_bgs();
void restore_bgm_bgs();
void set_verbose(bool on);
} // namespace audio
