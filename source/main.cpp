// main.cpp - OneShot WME PS3 port, milestone 2: maps, Niko, event interpreter.
//
// Controls (PS3):  d-pad / left stick = walk,  Square (hold) = run,
//                  Cross = OK / interact,  Circle = cancel,
//                  SELECT = toggle debug overlay. While the overlay is shown:
//                  L1 / R1 = previous / next map, R2/L2/R3/L3 = renderer
//                  diagnostics, Circle = plain shader, Triangle = texture viewer.
// Host build: env OSPS3_DATA (data dir), OSPS3_MAP, OSPS3_SPAWN="x,y",
//             OSPS3_INPUT (scripted buttons), OSPS3_SHOTS="frame:file.png,...".
#include "platform/gfx.h"
#include "platform/input.h"
#include "platform/sys.h"
#include "engine/gm.h"
#include "engine/overlay.h"
#include "game/game_state.h"
#include "game/tilemap.h"
#include "game/player.h"
#include "game/pictures.h"
#include "engine/audio.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

#ifdef __PPU__
#include <sys/process.h>
SYS_PROCESS_PARAM(1001, 0x100000);
#endif

// A new game starts on map 1, whose autorun 'init' event shows the wake-up
// CGs and transfers to Niko's room (map 2).
static const int NEW_GAME_MAP = 1;
static const float NEW_GAME_X = 17, NEW_GAME_Y = 17;   // OneshotWindow: LoadMap(1, 17, 17)

static bool map_exists(int id) {
	char rel[96];
	snprintf(rel, sizeof(rel), "gamedata/maps/map%d.tmx", id);
	return sys::file_exists(rel);
}

static void fatal_screen(const char *msg) {
	sys::log("FATAL: %s\n", msg);
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
	sys::log("OneShot PS3 port - milestone 1\n");
	if (!gfx::init()) return 1;
	gm::init();
	input::init();

	if (!sys::find_data_dir(argc > 0 ? argv[0] : nullptr)) {
		fatal_screen("Game data not found.");
		gfx::shutdown();
		return 0;
	}

	GameState state;
	TileMap map(&state);
	if (!map.LoadGlobalData()) { fatal_screen("Could not read gamedata/oneshot_tilesets.json"); return 0; }

	int mapId = NEW_GAME_MAP;
	float sx = NEW_GAME_X, sy = NEW_GAME_Y;
	if (const char *m = getenv("OSPS3_MAP")) mapId = atoi(m);
	if (const char *s = getenv("OSPS3_SPAWN")) sscanf(s, "%f,%f", &sx, &sy);
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
	audio::set_verbose(getenv("OSPS3_AUDIO_LOG") != nullptr);
	if (!map.LoadMap(mapId, sx, sy, Direction::Down)) { fatal_screen("Could not load the start map."); return 0; }
	// The player starts without a sprite; the intro's "change party member"
	// gives it Niko's. Debug starts elsewhere need it straight away.
	if (mapId != NEW_GAME_MAP) map.GetPlayer()->SetNPCSheet("niko", 0);

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

	while (!input::quit_requested()) {
		input::update();
		if (input::pressed(input::BTN_SELECT)) show_overlay = !show_overlay;
		// Diagnostics only while the overlay is up: the face buttons belong to the game.
		bool dbg = show_overlay || tex_view;
		if (dbg && input::pressed(input::BTN_R2)) gfx::set_draw_mode(gfx::draw_mode() + 1);
		if (dbg && input::pressed(input::BTN_MASHTEXT)) gfx::set_full_sync(!gfx::full_sync());   // L2
		if (dbg && input::pressed(input::BTN_R3)) gfx::set_texel_coords(!gfx::texel_coords());
		if (dbg && input::pressed(input::BTN_L3)) map.debugLayers = (map.debugLayers + 1) % 4;
		if (dbg && input::pressed(input::BTN_CANCEL)) gfx::set_plain_shader(!gfx::plain_shader());   // Circle
		if (dbg && input::pressed(input::BTN_INVENTORY)) tex_view = !tex_view;                         // Triangle
		if (tex_view && input::pressed(input::BTN_RIGHT)) tex_view_idx++;
		if (tex_view && input::pressed(input::BTN_LEFT)) tex_view_idx--;
		if (dbg && (input::pressed(input::BTN_L1) || input::pressed(input::BTN_R1))) {
			int dir = input::pressed(input::BTN_R1) ? 1 : -1;
			int next = mapId;
			for (int tries = 0; tries < 400; tries++) {
				next += dir;
				if (next < 1) next = 400;
				if (next > 400) next = 1;
				if (map_exists(next)) break;
			}
			if (map.LoadMap(next, 0, 0, Direction::Down)) {
				mapId = next;
				Vec2 ms = map.GetMapSize();
				map.GetPlayer()->SetPosTile(map.FindOpenTileNear(ms.x / 2, ms.y / 2));
				map.Update();
			}
		}

		uint64_t t0 = sys::time_us();
		if (!tex_view) {
			// OneshotWindow.GameLogic order.
			map.Update();
			map.EventRunnerUpdate();
			pictures().Update();
			flash().Update();
		}
		mapId = map.GetMapID();
		uint64_t t1 = sys::time_us();

		gfx::begin_frame(0xff000000);   // may wait for the RSX (vsync-paced)
		uint64_t tb = sys::time_us();
		if (!tex_view) {
			// OneshotWindow.PreDraw order.
			gm::clip_to_window();
			map.Draw();
			pictures().Draw();
			flash().Draw();
			map.DrawEventRunners();
			map.DrawMapTransition();
			gm::clip_off();
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
				overlay::text(16, 16, 2, 0xffffff, "texture %d/%d: %s (%ux%u)  d-pad L/R: next   Triangle: close",
				              i + 1, n, names[i], t->w, t->h);
			}
		}
		uint64_t t2 = sys::time_us();
		if (show_overlay && !tex_view) {
			gfx::Stats st = gfx::last_stats();
			Vec2 pp = map.GetPlayer()->GetPixelPos();
			overlay::text(16, 16, 2, 0xffffff, "map %d  frame %.2f ms (%.1f fps)", mapId, frame_ms, 1000.0f / frame_ms);
			overlay::text(16, 46, 2, 0xc0ffc0, "cpu: update %.2f ms  draw %.2f ms  (idle/vsync %.2f ms)", update_ms, draw_ms, wait_ms);
			overlay::text(16, 76, 2, 0xc0e0ff, "draw calls %d  sprites %d  entities %d", st.draw_calls, st.quads, map.EntityCount());
			overlay::text(16, 106, 2, 0xffe0a0, "vram tex %.1f / %.1f MB  niko px %d,%d", st.tex_bytes_used / 1048576.0f,
			              st.tex_bytes_total / 1048576.0f, pp.x, pp.y);
			overlay::text(16, 136, 2, 0xff90ff, "rsx draw mode: %s  (R2 to cycle)", gfx::draw_mode_name());
			static const char *layer_names[] = { "all", "no entities", "no upper layers", "base layers only" };
			overlay::text(16, 166, 2, 0xff90ff, "L2 full sync: %s   R3 texcoords: %s   L3 layers: %s",
			              gfx::full_sync() ? "ON" : "off", gfx::texel_coords() ? "texel" : "normalized",
			              layer_names[map.debugLayers]);
			overlay::text(16, 196, 2, 0xff90ff, "Circle shader: %s   Triangle: texture viewer", gfx::plain_shader() ? "PLAIN" : "full");
			overlay::text(16, 226, 2, 0xa0a0a0, "L1/R1 change map   SELECT hide");
			overlay::text(16, 256, 2, 0xffc080, "script: %s  parallel %d  common %d  pictures %d",
			              map.IsInScript() ? "RUNNING" : "idle", map.ParallelRunnerCount(), map.CommonEventCount(),
			              pictures().Count());
			overlay::text(16, 286, 2, 0xffc080, "debug: PC password %04d (var 55)", state.var(55));
		}
		gfx::end_frame();

		uint64_t now = sys::time_us();
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
	gfx::shutdown();
	return 0;
}
