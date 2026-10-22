// input_host.cpp - scripted input for headless host runs.
//
// OSPS3_INPUT="R30 D20 r40 U10 ."  -> a sequence of steps, each a set of
// held buttons followed by a frame count. Letters: U D L R (d-pad), r = run
// + right, x=OK, o=Cancel, s=Run, i=Triangle, m=Start,
// e=SELECT, l=L1, k=R1, '.' = idle. Script end => quit.
#include "../input.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

namespace input {

struct Step { uint32_t buttons; int frames; };
static std::vector<Step> steps;
static size_t step_i;
static int step_left;
static uint32_t cur, prev;
static bool done;

void init() {
	const char *s = getenv("OSPS3_INPUT");
	if (!s) s = ". 2";
	while (*s) {
		while (*s == ' ') s++;
		if (!*s) break;
		uint32_t b = 0;
		while (*s && !isdigit((unsigned char)*s) && *s != ' ') {
			switch (*s) {
			case 'U': b |= BTN_UP; break;
			case 'D': b |= BTN_DOWN; break;
			case 'L': b |= BTN_LEFT; break;
			case 'R': b |= BTN_RIGHT; break;
			case 'x': b |= BTN_OK; break;
			case 'o': b |= BTN_CANCEL; break;
			case 's': b |= BTN_RUN; break;
			case 'i': b |= BTN_INVENTORY; break;   // Triangle
			case 'm': b |= BTN_MAINMENU; break;    // Start
			case 'e': b |= BTN_SELECT; break;
			case 'l': b |= BTN_L1; break;
			case 'k': b |= BTN_R1; break;
			case '.': break;
			}
			s++;
		}
		while (*s == ' ') s++;
		int n = atoi(s);
		while (isdigit((unsigned char)*s)) s++;
		steps.push_back({ b, n > 0 ? n : 1 });
	}
	step_i = 0;
	step_left = steps.empty() ? 0 : steps[0].frames;
}

void update() {
	prev = cur;
	if (step_i >= steps.size()) { done = true; cur = 0; tick_hold_counters(cur); return; }
	cur = steps[step_i].buttons;
	tick_hold_counters(cur);
	if (--step_left <= 0) {
		step_i++;
		if (step_i < steps.size()) step_left = steps[step_i].frames;
	}
}

bool held(uint32_t b) { return (cur & b) != 0; }
bool pressed(uint32_t b) { return (cur & b) && !(prev & b); }
int stick_x() { return 0; }
int stick_y() { return 0; }
bool quit_requested() { return done; }
int system_events() { return 0; }

} // namespace input
