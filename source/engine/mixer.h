// mixer.h - software audio mixer (the FMOD replacement).
//
// Threading: the game thread calls the control functions (play/stop/
// volume/...), which only push commands into a lock-free single-producer /
// single-consumer queue. The audio thread calls render(), which applies
// queued commands and then mixes. The game thread never waits on the
// audio thread, and the audio thread never blocks on the game thread.
//
// Voices play either a PCM buffer (sound effects, loaded once and never
// freed) or a Vorbis stream (music; the compressed file is held in memory
// and decoded as it plays). Each voice has volume, pitch (playback-rate
// change, like FMOD setPitch), looping and pause, and is resampled to the
// output rate with linear interpolation.
#pragma once
#include <stdint.h>

namespace mixer {

struct Pcm {
	int16_t *data = nullptr;   // interleaved
	uint32_t frames = 0;
	int channels = 0;          // 1 or 2
	int rate = 0;
};

struct Stream;                 // Vorbis stream; owned by the voice it is played on
// Takes ownership of `file` (malloc'd Ogg data). Returns null on a bad file.
Stream *open_vorbis(uint8_t *file, uint32_t size);
// Free a stream that was opened but never played.
void close_stream(Stream *s);

enum { MAX_VOICES = 32 };

bool init(int outputRate);
int output_rate();

// Control (game thread).
void play_pcm(int voice, const Pcm *pcm, float volume, float pitch, bool loop);
void play_stream(int voice, Stream *s, float volume, float pitch, bool loop);   // takes ownership
void stop(int voice);
void set_volume(int voice, float volume);
void set_pitch(int voice, float pitch);
void set_paused(int voice, bool paused);
bool is_playing(int voice);   // false once a non-looping voice ended or was stopped

// Audio thread: mix `frames` stereo frames (interleaved L,R floats) into out.
void render(float *out, int frames);

// Statistics for the debug overlay.
int active_voices();
float render_load();   // fraction of real time spent in render (smoothed)

} // namespace mixer
