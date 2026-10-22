// main.cpp - OneShot WME PS3 port, milestone 2: maps, Niko, event interpreter.
//
// Controls (PS3):  d-pad / left stick = walk,  Square (hold) = run,
//                  Cross = OK / interact,  Circle = cancel,
//                  SELECT = toggle debug overlay (hold 1 s: mark the log).
//                  While the overlay is shown:
//                  L1 = map switcher, R1 = texture viewer. In the texture
//                  viewer: R2/L2/R3/L3 = renderer diagnostics, Circle =
//                  plain shader.
// Host build: env OSPS3_DATA (data dir), OSPS3_MAP, OSPS3_SPAWN="x,y",
//             OSPS3_INPUT (scripted buttons), OSPS3_SHOTS="frame:file.png,...".
#include "platform/gfx.h"
#include "platform/input.h"
#include "platform/sys.h"
#include "engine/gm.h"
#include "engine/font.h"
#include "engine/util.h"
#include "engine/overlay.h"
#include "game/game_state.h"
#include "game/tilemap.h"
#include "game/player.h"
#include "game/pictures.h"
#include "game/menus.h"
#include "game/modal.h"
#include "game/oneshot_window.h"
#include "game/debug_map_menu.h"
#include "game/settings.h"
#include "game/mouse_cursor.h"
namespace script_trace { void dump(); }
static void script_trace_dump() { script_trace::dump(); }
#include "engine/audio.h"

#include <stdio.h>
#define OSPS3_VERSION "m10g"
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

#ifdef __PPU__
#include <sys/process.h>
SYS_PROCESS_PARAM(1001, 0x100000);
// The toolchain's malloc arena defaults to 64 MB, which the game outgrows
// (decoded sound effects, music files, the GPU upload area, large tilesets
// while they are decoded). A PS3 game process has about 213 MB of user
// memory; 128 MB leaves ample room for the system's own allocations.
PS3TC_HEAP_SIZE(UINT64_C(128) * 1024 * 1024);
#endif

// A new game starts on map 1, whose autorun 'init' event shows the wake-up
// CGs and transfers to Niko's room (map 2).
static const int NEW_GAME_MAP = 1;
static const float NEW_GAME_X = 17, NEW_GAME_Y = 17;   // OneshotWindow: LoadMap(1, 17, 17)

static void fatal_screen(const char *msg) {
	sys::log("FATAL: %s\n", msg);
	sys::log_flush();
	while (!input::quit_requested()) {
		input::update();
		gfx::begin_frame(0xff601818);
		overlay::text(40, 40, 3, 0xffffff, "OneShot PS3 port");
		overlay::text(40, 100, 2, 0xffd0d0, "%s", msg);
		overlay::text(40, 140, 2, 0xffd0d0, "Copy your game's 'content' and 'gamedata' folders to");
		overlay::text(40, 170, 2, 0xffd0d0, "/dev_hdd0/game/OSWM00001/USRDIR/");
		gfx::end_frame();
#ifndef __PPU__
		break;
#endif
	}
}

int main(int argc, const char **argv) {
	sys::init_log();
	sys::log("OneShot PS3 port %s (built %s %s)\n", OSPS3_VERSION, __DATE__, __TIME__);
	if (!gfx::init()) return 1;
	gm::init();
	input::init();

	if (!sys::find_data_dir(argc > 0 ? argv[0] : nullptr)) {
		fatal_screen("Game data not found.");
		gfx::shutdown();
		return 0;
	}

	audio::init();
	audio::preload_sfx();
	sys::log("heap: %d MB in use after loading sound effects\n", sys::heap_used_kb() / 1024);

	// Debug starts (OSPS3_MAP, or the map browser) skip the title screen.
	const char *debugMap = getenv("OSPS3_MAP");
	if (!window().Boot(debugMap == nullptr)) { fatal_screen("Could not read gamedata/oneshot_tilesets.json"); return 0; }
	audio::set_verbose(getenv("OSPS3_AUDIO_LOG") != nullptr);
	{
		GameState &state = window().State();
		if (const char *f = getenv("OSPS3_FLAGS"))   // host testing: "178,160"
			for (const char *p = f; *p; ) { state.set_flag(atoi(p), true); while (*p && *p != ',') p++; if (*p) p++; }
		if (const char *v = getenv("OSPS3_VARS"))    // host testing: "3=2,4=1"
			for (const char *p = v; *p; ) {
				int id = atoi(p);
				while (*p && *p != '=') p++;
				if (*p) p++;
				state.set_var(id, atoi(p));
				while (*p && *p != ',') p++;
				if (*p) p++;
			}
		if (const char *it = getenv("OSPS3_ITEMS"))  // host testing: "3,4"
			for (const char *p = it; *p; ) { state.add_item(atoi(p)); while (*p && *p != ',') p++; if (*p) p++; }
	}
	int mapId = 0;
	if (debugMap) {
		float sx = NEW_GAME_X, sy = NEW_GAME_Y;
		mapId = atoi(debugMap);
		if (const char *s = getenv("OSPS3_SPAWN")) sscanf(s, "%f,%f", &sx, &sy);
		TileMap &map = window().Map();
		if (!map.LoadMap(mapId, sx, sy, Direction::Down)) { fatal_screen("Could not load the start map."); return 0; }
		// The player starts without a sprite; the intro's "change party member"
		// gives it Niko's. Debug starts elsewhere need it straight away.
		if (mapId != NEW_GAME_MAP) map.GetPlayer()->SetNPCSheet("niko", 0);
	}

	// Host testing: OSPS3_TEXTS="text1|text2" runs Show Text commands.
	if (const char *tx = getenv("OSPS3_TEXTS")) {
		std::vector<std::string> v;
		std::string all(tx);
		size_t p = 0;
		while (true) {
			size_t e = all.find('|', p);
			v.push_back(all.substr(p, e == std::string::npos ? std::string::npos : e - p));
			if (e == std::string::npos) break;
			p = e + 1;
		}
		window().Map().DebugRunTexts(v);
	}

	// Host screenshot schedule.
	struct Shot { int frame; std::string file; };
	std::vector<Shot> shots;
	if (const char *s = getenv("OSPS3_SHOTS")) {
		std::string all(s);
		size_t p = 0;
		while (p < all.size()) {
			size_t c = all.find(',', p);
			std::string item = all.substr(p, c == std::string::npos ? std::string::npos : c - p);
			size_t colon = item.find(':');
			if (colon != std::string::npos) shots.push_back({ atoi(item.c_str()), item.substr(colon + 1) });
			if (c == std::string::npos) break;
			p = c + 1;
		}
	}

	bool show_overlay = getenv("OSPS3_OVERLAY") != nullptr;
	bool tex_view = getenv("OSPS3_TEXVIEW") != nullptr;
	int tex_view_idx = getenv("OSPS3_TEXVIEW") ? atoi(getenv("OSPS3_TEXVIEW")) : 0;
	uint64_t last = sys::time_us();
	float frame_ms = 16.7f, update_ms = 0, draw_ms = 0, wait_ms = 0;
	int frame = 0;

	while (!input::quit_requested() && !window().QuitRequested()) {
		prof::begin();
		input::update();
		prof::mark("input");
		{
			// After a system (XMB) event, log the display state once a second
			// for ten seconds.
			static int lastEvents, linesLeft;
			static uint64_t nextLine;
			if (input::system_events() != lastEvents) { lastEvents = input::system_events(); linesLeft = 10; nextLine = 0; }
			if (linesLeft > 0 && sys::time_us() >= nextLine) {
				char st[512];
				gfx::debug_status(st, sizeof(st));
				sys::log("display: frame %d, %s\n", frame, st);
				linesLeft--;
				nextLine = sys::time_us() + 1000000;
			}
		}
		TileMap *mp = &window().Map();
		bool mapMenu = debug_map_menu::is_open();
		// SELECT: tap = debug overlay, hold one second = mark the log (a bug
		// report bookmark with the game state and the last script commands).
		static int selHeld;
		if (input::held(input::BTN_SELECT)) {
			if (mapMenu && selHeld == 0) selHeld = 1000;   // closing the map switcher: neither toggle nor mark
			if (++selHeld == 60) {
				static int marks;
				TileMap &m = window().Map();
				GameState &st = window().State();
				Vec2 pp = m.GetPlayer() ? m.GetPlayer()->GetPixelPos() : Vec2();
				GameTone tn = m.GetScreenTone();
				sys::log("=== MARK %d === map %d, Niko at tile %d,%d (px %d,%d), play time %lld frames\n", ++marks, m.GetMapID(),
				         pp.x / 16, pp.y / 16, pp.x, pp.y, st.play_frames);
				sys::log("  script %s, %d parallel; map transition %s; screen tone %d,%d,%d (set %lld frames ago); %d pictures\n",
				         m.IsInScript() ? "RUNNING" : "idle", m.ParallelRunnerCount(), m.IsMapTransitioning() ? "running" : "no",
				         tn.r, tn.g, tn.b, m.FramesSinceToneChange(), pictures().Count());
				sys::log("  flags 11=%d 22=%d; vars 6=%d 7=%d 8=%d 10=%d 22=%d 23=%d\n", st.flag(11), st.flag(22), st.var(6), st.var(7),
				         st.var(8), st.var(10), st.var(22), st.var(23));
				script_trace_dump();
				sys::log("=== end of mark %d ===\n", marks);
			}
		} else {
			if (selHeld > 0 && selHeld < 60 && !mapMenu) show_overlay = !show_overlay;
			selHeld = 0;
		}
		// Diagnostics only while the overlay is up: the face buttons belong to the game.
		bool dbg = (show_overlay || tex_view) && !mapMenu;
		// Renderer diagnostics only in the texture viewer, where the game is
		// paused: on the overlay, Circle / L2 / L3 are also game buttons, and
		// Circle once switched the shader that draws screen tones (fades) off.
		{
			bool diag = tex_view && !mapMenu;
			bool changed = true;
			if (diag && input::pressed(input::BTN_R2)) gfx::set_draw_mode(gfx::draw_mode() + 1);
			else if (diag && input::pressed(input::BTN_MASHTEXT)) gfx::set_vertex_main_memory(!gfx::vertex_main_memory());   // L2
			else if (diag && input::pressed(input::BTN_R3)) gfx::set_texel_coords(!gfx::texel_coords());
			else if (diag && input::pressed(input::BTN_L3)) mp->debugLayers = (mp->debugLayers + 1) % 4;
			else if (diag && input::pressed(input::BTN_CANCEL)) gfx::set_plain_shader(!gfx::plain_shader());   // Circle
			else if (diag && input::pressed(input::BTN_RUN)) gfx::set_present_copy(!gfx::present_copy());     // Square
			else changed = false;
			if (changed)
				sys::log("debug: renderer settings now: draw mode %s, vertices %s, texcoords %s, layers %d, shader %s\n",
				         gfx::draw_mode_name(), gfx::vertex_main_memory() ? "main" : "local", gfx::texel_coords() ? "texel" : "normalized",
				         mp->debugLayers, gfx::plain_shader() ? "PLAIN" : "full");
		}
		if (dbg && input::pressed(input::BTN_R1)) tex_view = !tex_view;
		if (tex_view && input::pressed(input::BTN_RIGHT)) tex_view_idx++;
		if (tex_view && input::pressed(input::BTN_LEFT)) tex_view_idx--;
		// Cursor test (overlay + R3): the World Machine desktop's cursor on top
		// of the game. Right stick moves, R2 clicks, L3 changes its shape.
		static bool cursorTest;
		static MouseCursor cursor;
		static int cursorShape;
		if (dbg && !tex_view && input::pressed(input::BTN_R3)) {
			cursorTest = !cursorTest;
			if (cursorTest) cursor.SetMousePos(Vec2(desktop_space().x / 2, desktop_space().y / 2));
			sys::log("cursor test %s (desktop space %dx%d)\n", cursorTest ? "on" : "off", desktop_space().x, desktop_space().y);
		}
		if (cursorTest) {
			static const MouseCursor::State shapes[6] = { MouseCursor::Normal, MouseCursor::Clickable, MouseCursor::Grabbable,
			                                              MouseCursor::Holding, MouseCursor::Move, MouseCursor::NotAllowed };
			if (input::pressed(input::BTN_L3)) cursorShape = (cursorShape + 1) % 6;
			cursor.forceState = true;
			cursor.forcedState = shapes[cursorShape];
			cursor.Update();
			if (cursor.Clicked()) sys::log("cursor test: click at %d,%d\n", cursor.MousePos().x, cursor.MousePos().y);
		}

		// The map switcher waits for menus and dialogs to close (they own input).
		if (dbg && !tex_view && input::pressed(input::BTN_L1) && !modal::is_open() && !window().Menus().IsMenuOpen())
			debug_map_menu::open(mapId);

		prof::mark("input/debug");
		uint64_t t0 = sys::time_us();
		if (mapMenu) {
			debug_map_menu::update();   // the game is paused underneath
		} else if (!tex_view) {
			modal::update();
			window().Update();   // OneshotWindow.GameLogic
		}
		TileMap &map = window().Map();
		GameState &state = window().State();
		mapId = map.GetMapID();
		uint64_t t1 = sys::time_us();

		prof::mark("game update");
		gfx::begin_frame(0xff000000);   // may wait for the RSX (vsync-paced)
		prof::mark("begin_frame (RSX wait)");
		uint64_t tb = sys::time_us();
		if (!tex_view) {
			gm::set_scaling(settings().scaling);
			gm::begin_window();  // offscreen at a whole-number scale (gm.h)
			gm::clip_to_window();
			window().Draw();     // OneshotWindow.PreDraw
			gm::clip_off();
			gm::end_window();
			modal::draw();
		} else {
			// Texture viewer: raw texture over a magenta/grey checkerboard so
			// transparent texels are obvious. Integer scale, centred.
			const char *names[256];
			const gfx::Texture *texs[256];
			int n = gm::cache_list(gm::Game, names, texs, 256);
			if (n > 0) {
				int i = ((tex_view_idx % n) + n) % n;
				const gfx::Texture *t = texs[i];
				int sw = gfx::screen_w(), sh = gfx::screen_h() - 60;
				int sc = 1;
				while ((int)t->w * (sc + 1) <= sw && (int)t->h * (sc + 1) <= sh) sc++;
				float w = (float)(t->w * sc), h = (float)(t->h * sc);
				if (h > sh) { h = (float)sh; w = h * t->w / t->h; }   // very tall sheets: squash to fit
				float x0 = (sw - w) * 0.5f, y0 = 60.0f;
				gfx::Shade a, b;
				a.r = 1; a.g = 0; a.b = 1; b.r = b.g = b.b = 0.5f;
				for (float cy = y0; cy < y0 + h; cy += 16)
					for (float cx = x0; cx < x0 + w; cx += 16) {
						bool odd = (((int)((cx - x0) / 16) + (int)((cy - y0) / 16)) & 1) != 0;
						gm::blit_screen(gm::white(), cx, cy, 16, 16, Rect(0, 0, 1, 1), odd ? a : b);
					}
				gfx::Shade s;
				gm::blit_screen(t, x0, y0, w, h, Rect(0, 0, (int)t->w, (int)t->h), s);
				overlay::text(16, 16, 2, 0xffffff, "texture %d/%d: %s (%ux%u)  d-pad L/R: next   R1: close   R2/L2/R3/L3/Circle: renderer switches   Square: present by %s",
				              i + 1, n, names[i], t->w, t->h, gfx::present_copy() ? "copy" : "drawing");
			}
		}
		uint64_t t2 = sys::time_us();
		if (debug_map_menu::is_open()) {
			debug_map_menu::draw();
		} else if (show_overlay && !tex_view) {
			gfx::Stats st = gfx::last_stats();
			Vec2 pp = map.GetPlayer() ? map.GetPlayer()->GetPixelPos() : Vec2();
			overlay::text(16, 16, 2, 0xffffff, "map %d  frame %.2f ms (%.1f fps)", mapId, frame_ms, 1000.0f / frame_ms);
			overlay::text(16, 46, 2, 0xc0ffc0, "cpu: update %.2f ms  draw %.2f ms  (idle/vsync %.2f ms)", update_ms, draw_ms, wait_ms);
			overlay::text(16, 76, 2, 0xc0e0ff, "draw calls %d  sprites %d  entities %d", st.draw_calls, st.quads, map.EntityCount());
			overlay::text(16, 106, 2, 0xffe0a0, "vram tex %.1f / %.1f MB  niko px %d,%d", st.tex_bytes_used / 1048576.0f,
			              st.tex_bytes_total / 1048576.0f, pp.x, pp.y);
			overlay::text(16, 136, 2, 0xff90ff, "rsx draw mode: %s", gfx::draw_mode_name());
			static const char *layer_names[] = { "all", "no entities", "no upper layers", "base layers only" };
			overlay::text(16, 166, 2, 0xff90ff, "vertices: %s   texcoords: %s   layers: %s",
			              gfx::vertex_main_memory() ? "main mem" : "local mem", gfx::texel_coords() ? "texel" : "normalized",
			              layer_names[map.debugLayers]);
			overlay::text(16, 196, 2, 0xff90ff, "shader: %s   R1: texture viewer (renderer switches are in there)",
			              gfx::plain_shader() ? "PLAIN" : "full");
			overlay::text(16, 226, 2, 0xa0a0a0, "L1: map switcher   R3: cursor test   SELECT: hide   hold SELECT: mark the log");
			overlay::text(16, 256, 2, 0xffc080, "script: %s  parallel %d  common %d  pictures %d",
			              map.IsInScript() ? "RUNNING" : "idle", map.ParallelRunnerCount(), map.CommonEventCount(),
			              pictures().Count());
			overlay::text(16, 316, 2, 0xa0e0ff, "scaling: %s", gm::scaling_info());
			overlay::text(16, 286, 2, 0xffc080, "debug: PC password %04d (var 55)   safe code %06d (var 20)   log file: %s",
			              state.var(55), state.var(20), sys::log_file_ok() ? "ok" : "NOT WRITABLE");
		}
		if (cursorTest) {
			static const char *names[6] = { "normal", "clickable", "grabbable", "holding", "move", "not allowed" };
			overlay::text(16, gfx::screen_h() - 50.0f, 2, 0xffffff,
			              "cursor test: %d,%d of %dx%d  %s  shape: %s   right stick: move  R2: click  L3: shape  overlay+R3: off",
			              cursor.MousePos().x, cursor.MousePos().y, desktop_space().x, desktop_space().y,
			              cursor.Held() ? "[HELD]" : "", names[cursorShape]);
			cursor.Draw();
		}
		prof::mark("overlay");
		gfx::end_frame();
		prof::mark("end_frame (flip)");

		uint64_t now = sys::time_us();
		{
			// Hitch log: frames well over budget (rate-limited; skips map-load frames).
			static uint64_t last_hitch_log;
			static int last_map = -1;
			float ms = (now - last) / 1000.0f;
			if (ms > 40.0f && mapId == last_map && now - last_hitch_log > 1000000) {
				Vec2 pp = map.GetPlayer() ? map.GetPlayer()->GetPixelPos() : Vec2();
				float pms;
				const char *phase = prof::worst(&pms);
				sys::log("slow frame: %.1f ms (update %.2f, draw %.2f; longest: %s %.1f ms) map %d at %d,%d\n", ms,
				         (t1 - t0) / 1000.0f,
				         (t2 - tb) / 1000.0f, phase, pms, mapId, pp.x / 16, pp.y / 16);
				last_hitch_log = now;
			}
			last_map = mapId;
		}
		frame_ms = frame_ms * 0.9f + ((now - last) / 1000.0f) * 0.1f;
		update_ms = update_ms * 0.9f + ((t1 - t0) / 1000.0f) * 0.1f;
		draw_ms = draw_ms * 0.9f + ((t2 - tb) / 1000.0f) * 0.1f;
		wait_ms = wait_ms * 0.9f + ((tb - t1) / 1000.0f) * 0.1f;
		last = now;
		frame++;
		for (auto &s : shots)
			if (s.frame == frame) gfx::screenshot(s.file.c_str());
	}
	sys::log("exit after %d frames\n", frame);
	sys::log_flush();
	audio::shutdown();
	gfx::shutdown();
	return 0;
}
