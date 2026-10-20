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
