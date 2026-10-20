// input_common.cpp - backend-independent input helpers.
#include "input.h"

namespace input {

static int hold[32];

void tick_hold_counters(uint32_t cur) {
	for (int i = 0; i < 32; i++) hold[i] = (cur & (1u << i)) ? hold[i] + 1 : 0;
}

int held_frames(uint32_t b) {
	for (int i = 0; i < 32; i++)
		if (b == (1u << i)) return hold[i];
	return 0;
}

bool auto_trigger(uint32_t b, int first, int interval) {
	int n = held_frames(b);
	return n >= first && n % interval == 0;
}

} // namespace input
