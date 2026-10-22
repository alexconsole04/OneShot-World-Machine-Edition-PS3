// audio_out_host.cpp - host audio "device": renders 1/60 s per game frame
// and optionally records it to a 16-bit WAV (OSPS3_AUDIO_WAV=path).
#include "../audio_out.h"
#include "../../engine/mixer.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <vector>

namespace audio_out {

static FILE *wav;
static uint32_t data_bytes;
static int g_rate;
static double carry;

static void put32(uint32_t v) { uint8_t b[4] = { (uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24) }; fwrite(b, 1, 4, wav); }
static void put16(uint16_t v) { uint8_t b[2] = { (uint8_t)v, (uint8_t)(v >> 8) }; fwrite(b, 1, 2, wav); }

static void header() {
	fseek(wav, 0, SEEK_SET);
	fwrite("RIFF", 1, 4, wav); put32(36 + data_bytes); fwrite("WAVEfmt ", 1, 8, wav);
	put32(16); put16(1); put16(2); put32(g_rate); put32(g_rate * 4); put16(4); put16(16);
	fwrite("data", 1, 4, wav); put32(data_bytes);
	fseek(wav, 0, SEEK_END);
}

bool start(int rate) {
	g_rate = rate;
	if (const char *p = getenv("OSPS3_AUDIO_WAV")) {
		wav = fopen(p, "wb");
		if (wav) header();
	}
	return true;
}

void stop() {
	if (wav) { header(); fclose(wav); wav = nullptr; }
}

static unsigned pumps;
unsigned mixed_blocks() { return pumps; }

void pump() {
	pumps++;
	carry += g_rate / 60.0;
	int frames = (int)carry;
	carry -= frames;
	std::vector<float> buf(frames * 2);
	mixer::render(buf.data(), frames);
	if (!wav) return;
	for (float f : buf) {
		int v = (int)(f * 32767.0f);
		put16((uint16_t)(int16_t)(v > 32767 ? 32767 : (v < -32768 ? -32768 : v)));
	}
	data_bytes += frames * 4;
	header();
}

} // namespace audio_out
