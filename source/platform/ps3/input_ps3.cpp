// input_ps3.cpp - DualShock 3 via libio pad.
#include "../input.h"
#include <string.h>
#include <math.h>
#include <io/pad.h>
#include <cell/sysutil.h>

namespace input {

static uint32_t cur, prev;
static int sx, sy;
static volatile bool quit;

static void on_sysutil(uint64_t status, uint64_t, void *) {
	if (status == CELL_SYSUTIL_REQUEST_EXITGAME) quit = true;
}

void init() {
	ioPadInit(7);
	cellSysutilRegisterCallback(0, on_sysutil, nullptr);
}

// Raw pad state persists between reads: ioPadGetData() reports len == 0
// when nothing changed since the last call, which is most frames while a
// stick is held still. (Treating that as "centred" made Niko stutter.)
struct RawPad { bool valid = false; padData d; };
static RawPad raw[MAX_PADS];

// Left stick -> -100..100 with a radial dead zone. The DS3 reports a square
// range (full diagonal reads ~141%), so the vector is clamped to the unit
// circle: equal speed in all directions, no diagonal boost.
static void stick(const padData &d, int &ox, int &oy) {
	float x = ((int)d.ANA_L_H - 128) / 127.0f;
	float y = ((int)d.ANA_L_V - 128) / 127.0f;
	float mag = sqrtf(x * x + y * y);
	const float dead = 0.20f;
	if (mag < dead) { ox = oy = 0; return; }
	float scaled = (mag - dead) / (1.0f - dead);
	if (scaled > 1.0f) scaled = 1.0f;
	ox = (int)(x / mag * scaled * 100.0f);
	oy = (int)(y / mag * scaled * 100.0f);
}

void update() {
	cellSysutilCheckCallback();
	prev = cur;
	cur = 0;
	sx = sy = 0;
	padInfo info;
	ioPadGetInfo(&info);
	for (int i = 0; i < MAX_PADS; i++) {
		if (!info.status[i]) { raw[i].valid = false; continue; }
		padData pd;
		memset(&pd, 0, sizeof(pd));
		ioPadGetData(i, &pd);
		if (pd.len > 0) { raw[i].d = pd; raw[i].valid = true; }
	}
	for (int i = 0; i < MAX_PADS; i++) {
		if (!raw[i].valid) continue;
		const padData &pd = raw[i].d;
		if (pd.BTN_DOWN) cur |= BTN_DOWN;
		if (pd.BTN_LEFT) cur |= BTN_LEFT;
		if (pd.BTN_RIGHT) cur |= BTN_RIGHT;
		if (pd.BTN_UP) cur |= BTN_UP;
		if (pd.BTN_CROSS) cur |= BTN_OK;
		if (pd.BTN_CIRCLE) cur |= BTN_CANCEL;
		if (pd.BTN_SQUARE) cur |= BTN_RUN;
		if (pd.BTN_TRIANGLE) cur |= BTN_INVENTORY;
		if (pd.BTN_START) cur |= BTN_MAINMENU;
		if (pd.BTN_L2) cur |= BTN_MASHTEXT;
		if (pd.BTN_SELECT) cur |= BTN_SELECT;
		if (pd.BTN_L1) cur |= BTN_L1;
		if (pd.BTN_R1) cur |= BTN_R1;
		if (pd.BTN_R2) cur |= BTN_R2;
		stick(pd, sx, sy);
		break;   // first connected pad drives the game
	}
}

bool held(uint32_t b) { return (cur & b) != 0; }
bool pressed(uint32_t b) { return (cur & b) && !(prev & b); }
int stick_x() { return sx; }
int stick_y() { return sy; }
bool quit_requested() { return quit; }

} // namespace input
