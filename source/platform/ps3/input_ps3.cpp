// input_ps3.cpp - DualShock 3 via libio pad.
#include "../input.h"
#include <string.h>
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

// Map raw 0..255 stick to -100..100 with a radial-ish dead zone,
// matching the game's MoveStickPos scale.
static int axis(int raw) {
	int v = raw - 128;
	const int dead = 24;
	if (v > -dead && v < dead) return 0;
	int s = v > 0 ? 1 : -1;
	int m = (v * s - dead) * 100 / (127 - dead);
	if (m > 100) m = 100;
	return s * m;
}

void update() {
	cellSysutilCheckCallback();
	prev = cur;
	cur = 0;
	sx = sy = 0;
	padInfo info;
	ioPadGetInfo(&info);
	for (int i = 0; i < MAX_PADS; i++) {
		if (!info.status[i]) continue;
		padData pd;
		memset(&pd, 0, sizeof(pd));
		ioPadGetData(i, &pd);
		if (pd.len == 0) { cur |= prev; continue; }   // no new data: keep state
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
		int x = axis(pd.ANA_L_H), y = axis(pd.ANA_L_V);
		if (x || y) { sx = x; sy = y; }
		break;   // first connected pad drives the game
	}
}

bool held(uint32_t b) { return (cur & b) != 0; }
bool pressed(uint32_t b) { return (cur & b) && !(prev & b); }
int stick_x() { return sx; }
int stick_y() { return sy; }
bool quit_requested() { return quit; }

} // namespace input
