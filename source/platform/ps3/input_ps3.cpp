// input_ps3.cpp - DualShock 3 via libio pad.
#include "../input.h"
#include <string.h>
#include <math.h>
#include <io/pad.h>
#include <cell/sysutil.h>
#include "../gfx.h"
#include "../sys.h"

namespace input {

static uint32_t cur, prev;
static int sx, sy, rx, ry;
static volatile bool quit;

static volatile int g_sys_events;
static volatile bool g_sys_drawing;   // between system drawing begin / end (XMB, system dialogs)

static uint64_t g_menu_opened_at;

static void on_sysutil(uint64_t status, uint64_t param, void *) {
	(void)param;
	switch (status) {
	case CELL_SYSUTIL_REQUEST_EXITGAME: quit = true; sys::log("system: quit requested\n"); break;
	case 0x0121: g_sys_drawing = true; gfx::set_system_drawing(true); break;      // system drawing begins
	case 0x0122: g_sys_drawing = false; gfx::set_system_drawing(false); gfx::system_menu_closed(); break;
	case 0x0131: g_menu_opened_at = sys::time_us(); sys::log("system: XMB opened\n"); break;
	case 0x0132: {
		char st[256];
		gfx::debug_status(st, sizeof(st));
		sys::log("system: XMB closed after %.1f s (%s)\n", (sys::time_us() - g_menu_opened_at) / 1e6, st);
		gfx::system_menu_closed();
		break;
	}
	case 0x0133: case 0x0134: break;   // XMB music started / stopped
	default: sys::log("system: event 0x%04x\n", (unsigned)status); break;
	}
	g_sys_events++;
}

int system_events() { return g_sys_events; }
bool system_drawing() { return g_sys_drawing; }

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
	sx = sy = rx = ry = 0;
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
		if (pd.BTN_L3) cur |= BTN_L3;
		if (pd.BTN_R3) cur |= BTN_R3;
		stick(pd, sx, sy);
		rx = ((int)pd.ANA_R_H - 128) * 100 / 127;
		ry = ((int)pd.ANA_R_V - 128) * 100 / 127;
		if (rx > 100) rx = 100;
		if (ry > 100) ry = 100;
		break;   // first connected pad drives the game
	}
	tick_hold_counters(cur);
}

bool held(uint32_t b) { return (cur & b) != 0; }
bool pressed(uint32_t b) { return (cur & b) && !(prev & b); }
int stick_x() { return sx; }
int stick_y() { return sy; }
int rstick_x() { return rx; }
int rstick_y() { return ry; }
bool quit_requested() { return quit; }

} // namespace input
