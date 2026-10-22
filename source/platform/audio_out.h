// audio_out.h - platform audio output: pulls mixed audio from mixer::render.
//   PS3: cellAudio port + a PPU thread woken by the mixer's event queue.
//   Host: no device; pump() renders one frame's worth per call and can
//         write it to a WAV file (OSPS3_AUDIO_WAV) for checking.
#pragma once

namespace audio_out {
bool start(int rate);
void stop();
void pump();   // called from audio::update() every game frame
unsigned mixed_blocks();   // mixer blocks rendered so far (diagnostics)
}
