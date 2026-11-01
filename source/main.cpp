// main.cpp - OneShot: World Machine Edition, PS3 port: start-up and the main loop.
//
// Debug controls (PS3): SELECT = toggle the debug overlay (hold 1 s: mark the
// log). While the overlay is shown: L1 = map switcher, R1 = texture viewer.
// In the texture viewer: R2/L2/R3/L3 = renderer diagnostics, Circle = plain
// shader.
// Host test switches (OSPS3_*) are read in test_hooks.cpp.
#include "test_hooks.h"
#include "engine/audio.h"
#include "engine/gm.h"
#include "engine/overlay.h"
#include "engine/util.h"
#include "game/core/game_state.h"
#include "game/core/loc.h"
#include "game/core/oneshot_window.h"
#include "game/core/settings.h"
#include "game/twm/desktop.h"
#include "game/twm/wm.h"
#include "game/ui/boot_sequence.h"
#include "game/ui/debug_map_menu.h"
#include "game/ui/menus.h"
#include "game/ui/modal.h"
#include "game/world/pictures.h"
#include "game/world/player.h"
#include "game/world/tilemap.h"
#include "platform/gfx.h"
#include "platform/input.h"
#include "platform/sys.h"
#include <exception>
#include <new>
#include <stdlib.h>
#include <string>

#ifdef __PPU__
#include <sys/process.h>
#endif

#define OSPS3_VERSION "m27"

namespace script_trace { void dump(); }   // event_runner.cpp: the last script commands run

#ifdef __PPU__
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

// Say so in the log when the C++ library gives up.
static void on_out_of_memory() {
	sys::log("FATAL: out of memory (heap %d KB in use)\n", sys::heap_used_kb());
	sys::log_flush();
	abort();
}
static void on_terminate() {
	sys::log("FATAL: std::terminate - a C++ library error (heap %d KB in use)\n", sys::heap_used_kb());
	sys::log_flush();
	abort();
}

int main(int argc, const char **argv) {
	sys::init_log();
	std::set_new_handler(on_out_of_memory);
	std::set_terminate(on_terminate);
	sys::log("OneShot PS3 port %s (built %s %s)\n", OSPS3_VERSION, __DATE__, __TIME__);
	if (!gfx::init()) return 1;
	gm::init();
	input::init();

	if (!sys::find_data_dir(argc > 0 ? argv[0] : nullptr)) {
		fatal_screen("Game data not found.");
		gfx::shutdown();
		return 0;
	}

	{
		// The PC seeds System.Random from the clock: every boot plays out
		// differently (George's personality, NPC wandering, glitches).
		// Host tests can pin it (OSPS3_SEED).
		int h, m, s, yd;
		sys::local_time(&h, &m, &s, &yd);
		uint32_t seed = test_hooks::seed((uint32_t)sys::time_us() ^ (uint32_t)(((yd * 24 + h) * 60 + m) * 60 + s) * 2654435761u);
		util::seed(seed);
		sys::log("random seed %08x\n", (unsigned)seed);
	}
	loc::init();
	settings().Load();   // the desktop's cursor uses the Controls settings from the start
	audio::init();
	// Test starts on a given map skip the boot sequence and start inside a
	// maximized OneShot window; "skip boot" starts on the desktop.
	const int debugMap = test_hooks::start_map();
	const bool bootSequence = !test_hooks::skip_boot();
	if (!bootSequence) {
		audio::preload_sfx();   // the boot console loads them otherwise
		sys::log("heap: %d MB in use after loading sound effects\n", sys::heap_used_kb() / 1024);
	}
	if (!window().Boot(false)) { fatal_screen("Could not read gamedata/oneshot_tilesets.json"); return 0; }
	boot::set_version("OneShot PS3 port " OSPS3_VERSION);
	if (bootSequence) boot::start();
	else {
		desktop::init();
		if (debugMap >= 0) desktop::ensure_game_window(true);
		else if (test_hooks::launch()) {
			desktop::ensure_game_window(false);
			window().Launch(true);
			if (settings().fullscreen) twm::set_game_maximized(true);
		}
	}
	audio::set_verbose(test_hooks::audio_log());
	test_hooks::apply_game_state(window().State());
	int mapId = 0;
	if (debugMap >= 0) {
		float sx = NEW_GAME_X, sy = NEW_GAME_Y;
		mapId = debugMap;
		test_hooks::start_position(sx, sy);
		TileMap &map = window().Map();
		if (!map.LoadMap(mapId, sx, sy, Direction::Down)) { fatal_screen("Could not load the start map."); return 0; }
		// The player starts without a sprite; the intro's "change party member"
		// gives it Niko's. Debug starts elsewhere need it straight away.
		if (mapId != NEW_GAME_MAP) map.GetPlayer()->SetNPCSheet("niko", 0);
	}

	test_hooks::run_texts();

	bool show_overlay = test_hooks::overlay();
	int tex_view_idx = 0;
	bool tex_view = test_hooks::texture_viewer(tex_view_idx);
	uint64_t last = sys::time_us();
	float frame_ms = 16.7f, update_ms = 0, draw_ms = 0, wait_ms = 0;
	int frame = 0;

	while (!input::quit_requested() && !window().QuitRequested() && !desktop::quit_requested()) {
		prof::begin();
		gm::pump_prefetch();   // textures read ahead in the background
		input::update();
		prof::mark("input");
		// XMB open (or another system overlay): pause the game's logic. Frames
		// keep being drawn and flipped - the XMB is drawn through the game's
		// flips, so stopping them froze the XMB itself.
		bool systemPause = input::system_drawing();
		{
			static bool was;
			if (systemPause != was) sys::log(systemPause ? "game paused while the XMB is open\n" : "game resumed\n");
			was = systemPause;
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
				script_trace::dump();
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
		// The map switcher waits for menus and dialogs to close (they own input).
		if (dbg && !tex_view && !boot::active() && input::pressed(input::BTN_L1) && !modal::is_open() && !window().Menus().IsMenuOpen())
			debug_map_menu::open(mapId);

		prof::mark("input/debug");
		uint64_t t0 = sys::time_us();
		{
			// Language chosen in the settings menu: the session goes, the boot
			// console runs again in the new language, then the desktop.
			std::string lang;
			if (int r = desktop::take_restart_request()) {
				// The Customize app: Change Language, or Erase All Data.
				window().ResetForBoot();
				desktop::close_all();
				if (r == 2) {
					desktop::erase_all_data();
					sys::log("restart: first launch after erasing all data\n");
					boot::start();
				} else {
					sys::log("restart: language select\n");
					boot::restart_language_select();
				}
			}
			if (boot::take_language_request(lang)) {
				sys::log("language change to %s: restarting the boot sequence\n", lang.c_str());
				window().ResetForBoot();
				desktop::close_all();
				loc::set(lang);
				boot::restart_console();
			}
		}
		bool booting = boot::active();
		if (systemPause) {
			// nothing: the picture stays as it was
		} else if (booting) {
			boot::update();
			if (boot::take_desktop_init()) desktop::init();   // Game1.InitWindowManager
			if (boot::just_finished()) sys::log("boot: finished at frame %d\n", frame);
		} else if (mapMenu) {
			debug_map_menu::update();   // the game is paused underneath
		} else if (!tex_view) {
			desktop::update();   // WindowManager.Update: the game runs inside its window
		}
		if (!systemPause) audio::update();
		// The frame the boot finishes on draws the desktop, not an empty boot
		// screen (that was a black frame between the logo wipe and the desktop).
		if (booting && !boot::active()) booting = false;
		TileMap &map = window().Map();
		GameState &state = window().State();
		mapId = map.GetMapID();
		uint64_t t1 = sys::time_us();

		prof::mark(booting ? "boot update" : "desktop update");
		gfx::begin_frame(0xff000000);   // may wait for the RSX (vsync-paced)
		prof::mark("begin_frame (RSX wait)");
		uint64_t tb = sys::time_us();
		if (booting) {
			if (boot::show_os()) {   // the desktop appears under the TWM logo
				desktop::draw();
				desktop::draw_cursor();
			}
			boot::draw();
		} else if (!tex_view) {
			desktop::draw();      // wallpaper, icons, windows (the game inside its own), taskbar
			desktop::draw_cursor();
			prof::mark("dialogs + cursor");
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
			overlay::text(16, 226, 2, 0xa0a0a0, "L1: map switcher   SELECT: hide   hold SELECT: mark the log");
			overlay::text(16, 256, 2, 0xffc080, "script: %s  parallel %d  common %d  pictures %d",
			              map.IsInScript() ? "RUNNING" : "idle", map.ParallelRunnerCount(), map.CommonEventCount(),
			              pictures().Count());
			overlay::text(16, 316, 2, 0xa0e0ff, "scaling: %s", gm::scaling_info());
			overlay::text(16, 286, 2, 0xffc080, "debug: PC password %04d (var 55)   safe code %06d (var 20)   log file: %s",
			              state.var(55), state.var(20), sys::log_file_ok() ? "ok" : "NOT WRITABLE");
		}

		prof::mark("overlay");
		gfx::end_frame();
		prof::mark("end_frame (flip)");
		{
			const char *what = booting ? "boot" : !desktop::game_window_open() ? "desktop" : "game window";
			prof::report(what);
		}

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
		test_hooks::screenshots(frame);
	}
	sys::log("exit after %d frames\n", frame);
	sys::save_flush();
	sys::log_flush();
	audio::shutdown();
	gfx::shutdown();
	return 0;
}
