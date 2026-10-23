#include <stdio.h>
#include "util.h"
#include <algorithm>
#include <ctype.h>
#include <string.h>

namespace util {

// xorshift32: deterministic, cheap, good enough for NPC wandering.
static uint32_t rng_state = 0x6f6e6573u;   // "ones"

void seed(uint32_t s) { rng_state = s ? s : 1; }

int random(int min, int max) {
	if (max < min) return min;
	uint32_t x = rng_state;
	x ^= x << 13; x ^= x >> 17; x ^= x << 5;
	rng_state = x;
	return min + (int)(x % (uint32_t)(max - min + 1));
}

float lerp(float a, float b, float t) { return a + (b - a) * t; }

std::string lower(std::string s) {
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)tolower(c); });
	return s;
}

bool starts_with(const std::string &s, const char *p) { return s.compare(0, strlen(p), p) == 0; }

} // namespace util

#include "../platform/sys.h"
namespace prof {
static uint64_t last, worstUs, frameStart;
static const char *worstName = "-";
// Totals over the current report period, per phase (string literals).
struct Total { const char *name; uint64_t us; };
static Total totals[48];
static int nTotals, frames, slowFrames;
static uint64_t frameUsSum, frameUsMax, periodStart;

void begin() {
	uint64_t now = sys::time_us();
	if (frameStart) {
		uint64_t f = now - frameStart;
		frames++;
		frameUsSum += f;
		if (f > frameUsMax) frameUsMax = f;
		if (f > 17500) slowFrames++;   // missed a 60 Hz vblank
	}
	frameStart = now;
	last = now;
	worstUs = 0;
	worstName = "-";
	if (!periodStart) periodStart = now;
}
void mark(const char *phase) {
	uint64_t now = sys::time_us();
	uint64_t d = now - last;
	if (d > worstUs) { worstUs = d; worstName = phase; }
	last = now;
	int i = 0;
	while (i < nTotals && totals[i].name != phase) i++;
	if (i == nTotals && nTotals < 48) totals[nTotals++] = { phase, 0 };
	if (i < nTotals) totals[i].us += d;
}
const char *worst(float *ms) { *ms = worstUs / 1000.0f; return worstName; }

void report(const char *what) {
	// Every 10 seconds: the frame rate and where the time per frame went.
	uint64_t now = sys::time_us();
	if (!periodStart || now - periodStart < 10000000 || frames == 0) return;
	char buf[512];
	int n = snprintf(buf, sizeof(buf), "perf (%s): %.1f fps, frame avg %.1f ms, max %.1f ms, %d of %d frames missed 60 Hz;", what,
	                 frames * 1e6 / (now - periodStart), frameUsSum / 1000.0 / frames, frameUsMax / 1000.0, slowFrames, frames);
	// The five biggest phases, average ms per frame.
	for (int k = 0; k < 5; k++) {
		int best = -1;
		for (int i = 0; i < nTotals; i++)
			if (totals[i].us && (best < 0 || totals[i].us > totals[best].us)) best = i;
		if (best < 0) break;
		n += snprintf(buf + n, sizeof(buf) - n, " %s %.2f", totals[best].name, totals[best].us / 1000.0 / frames);
		totals[best].us = 0;
	}
	sys::log("%s\n", buf);
	nTotals = frames = slowFrames = 0;
	frameUsSum = frameUsMax = 0;
	periodStart = now;
}
}
