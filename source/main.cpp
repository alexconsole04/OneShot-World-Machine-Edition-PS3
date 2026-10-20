// main.cpp - OneShot WME PS3 port, milestone 1: maps + Niko walking.
//
// Controls (PS3):  d-pad / left stick = walk,  Square (hold) = run,
//                  SELECT = toggle debug overlay,
//                  L1 / R1 = previous / next map (debug map browser).
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

#ifdef __PPU__
#include <sys/process.h>
SYS_PROCESS_PARAM(1001, 0x100000);
#endif

static const int NEW_GAME_MAP = 2;          // from events_map1 'init': transfer to map 2 (15,17) facing down
static const float NEW_GAME_X = 15, NEW_GAME_Y = 17;

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
	if (!map.LoadMap(mapId, sx, sy, Direction::Down)) { fatal_screen("Could not load the start map."); return 0; }

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
	uint64_t last = sys::time_us();
	float frame_ms = 16.7f, update_ms = 0, draw_ms = 0, wait_ms = 0;
	int frame = 0;

	while (!input::quit_requested()) {
		input::update();
		if (input::pressed(input::BTN_SELECT)) show_overlay = !show_overlay;
		if (input::pressed(input::BTN_R2)) gfx::set_draw_mode(gfx::draw_mode() + 1);
		if (input::pressed(input::BTN_MASHTEXT)) gfx::set_full_sync(!gfx::full_sync());   // L2
		if (input::pressed(input::BTN_R3)) gfx::set_texel_coords(!gfx::texel_coords());
		if (input::pressed(input::BTN_L3)) map.debugLayers = (map.debugLayers + 1) % 4;
		if (input::pressed(input::BTN_L1) || input::pressed(input::BTN_R1)) {
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
		map.Update();
		uint64_t t1 = sys::time_us();

		gfx::begin_frame(0xff000000);   // may wait for the RSX (vsync-paced)
		uint64_t tb = sys::time_us();
		gm::clip_to_window();
		map.Draw();
		gm::clip_off();
		uint64_t t2 = sys::time_us();
		if (show_overlay) {
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
			overlay::text(16, 196, 2, 0xa0a0a0, "L1/R1 change map   SELECT hide");
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
