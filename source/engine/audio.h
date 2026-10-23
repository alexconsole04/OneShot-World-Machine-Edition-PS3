// audio.h - SoundManager interface (ported from the PC version's
// SoundManager/FmodManager): BGM (music, streamed Ogg), BGS (looping
// ambient sound), ME (music effect jingle that ducks the BGM) and SE
// (sound effects). Call update() once per game frame.
#pragma once

namespace audio {
bool init();                 // starts the output; false = running silent
void shutdown();
void preload_sfx();          // load every gamedata/sfx/*.wav now (debug starts; the boot console does it otherwise)
// Boot console (SoundManager.LoadSoundList / NextSoundToLoad / LoadNextSound).
int scan_sounds();           // lists the sound effects and starts reading them; returns how many
int sound_count();
bool has_more_sounds();
const char *next_sound_name();
bool load_next_sound();      // false: the next file is still being read (try again next frame)
void load_all_sounds();      // the rest, waiting for each
void update();

void play_sound(const char *name, float vol = 1.0f, float pitch = 1.0f);
void play_song(const char *name, float fadeIn, float vol, float pitch);
void queue_song(const char *name, float fadeIn, float vol, float pitch);
void play_bgs(const char *name, float vol, float pitch);
void stop_bgs();
void play_me(const char *name, float vol, float pitch);
void fade_out_bgm(float seconds);
void fade_in_bgm(float targetVolume, float seconds);
void fade_out_bgs(float seconds);
void set_song_paused(bool paused);
void memorize_bgm_bgs();
void restore_bgm_bgs();

// SoundSaveData: the current BGM and BGS.
struct SaveData {
	char bgm[64] = "", bgs[64] = "";
	float bgmVol = 1, bgmPitch = 1, bgsVol = 1, bgsPitch = 1;
};
SaveData get_save_data();
void load_save_data(const SaveData &d);   // plays them
void stop_all();                          // music, ambience, jingles, sounds

void set_volumes(int bgm, int sfx);   // settings, 0..100
const char *current_song();
bool output_ok();
void set_verbose(bool on);
} // namespace audio
