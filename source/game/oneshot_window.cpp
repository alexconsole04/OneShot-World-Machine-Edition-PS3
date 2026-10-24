#include "oneshot_window.h"
#include "loc.h"
#include "modal.h"
#include "pictures.h"
#include "player.h"
#include "save.h"
#include "settings.h"
#include "json_file.h"
#include "twm/wm.h"
#include <math.h>
#include "../engine/audio.h"
#include "../engine/font.h"
#include "../engine/gm.h"
#include "../engine/util.h"
#include "../platform/input.h"
#include "../platform/sys.h"

OneshotWindow &window() {
	static OneshotWindow w;
	return w;
}

// OneshotWindow.StartGame: map 1's autorun "init" event either loads the
// save (save_exists -> real_load) or starts the intro.
static const int NEW_GAME_MAP = 1;
static const float NEW_GAME_X = 17, NEW_GAME_Y = 17;

bool OneshotWindow::NewSession() {
	session.reset();   // tear the old one down first (it may hold big textures/streams)
	film.active = false;
	if (shake.total > 0) twm::set_game_window_pos(shake.start);   // mid-shake: back where it was
	shake.total = shake.timer = 0;
	glitchTimer = 0;
	border.mapId = -1;
	border.shifting = false;
	pictures().Clear();
	flash() = FlashManager();
	audio::stop_all();
	sys::trace("session: new");
	session.reset(new Session());
	Session &s = *session;
	s.state.player_name = sys::user_name();
	if (!s.map.LoadGlobalData()) return false;
	s.menus.itemMenu.LoadData();
	sys::trace("session: items loaded");
	s.map.menus = &s.menus;
	s.menus.mainMenu.onQuit = [this] { CloseGame(); };
	// The constructor of the PC's OneshotWindow: settings, then permanent data.
	settings().Load();
	sys::trace("session: settings loaded");
	save::LoadPerma(&s.state);
	sys::trace("session: ready");
	return true;
}

bool OneshotWindow::Boot(bool showTitle) {
	if (!NewSession()) return false;
	// A quick check for an incomplete copy of the game files, so the title
	// screen can say so before anything fails later.
	static const char *needed[] = { "gamedata/maps/map1.tmx", "gamedata/maps/map2.tmx", "gamedata/music/MyBurdenIsLight.ogg",
	                                "gamedata/sfx/menu_cursor.wav", "content/titles/normal.xnb", "content/npc/niko.xnb" };
	int missing = 0;
	for (const char *f : needed)
		if (!sys::file_exists(f)) { sys::log("WARNING: missing %s\n", f); missing++; }
	std::vector<std::string> maps;
	if (sys::list_dir("gamedata/maps", maps)) sys::log("gamedata/maps holds %d files\n", (int)maps.size());
	if (missing) dataWarning = "Some game files are missing - see oneshot_log.txt";
	title.onChosen = [this](int choice) {
		if (choice == TitleScreen::Exit) {
			if (desktopMode) ExitGame();   // closes the window, back to the desktop
			else quitApp = true;
		}
		else StartGame();
	};
	if (showTitle) { open = true; OpenTitle(); }
	return true;
}

void OneshotWindow::OpenTitle() {
	session->menus.titleOpen = true;
	title.Open(&session->state, &session->menus);
}

void OneshotWindow::StartGame() {
	session->menus.titleOpen = false;
	sys::log("start game (%s)\n", save::Exists() ? "continue" : "new");
	session->map.LoadMap(NEW_GAME_MAP, NEW_GAME_X, NEW_GAME_Y, Direction::Down);
}

bool OneshotWindow::DebugTeleport(int mapId, int x, int y, Direction facing) {
	if (!open) {
		Launch(false);
		if (onOpenedForDebug) onOpenedForDebug();
	}
	if (title.IsOpen()) {
		title.ForceClose();
		session->menus.titleOpen = false;
		audio::fade_out_bgm(0.25f);
	}
	pictures().Clear();
	flash() = FlashManager();
	return session->map.DebugTeleport(mapId, (float)x, (float)y, facing);
}

void OneshotWindow::ExitGame() { exitRequested = true; }

void OneshotWindow::Launch(bool startGame) {
	// new OneshotWindow(): a fresh session, then straight into the game if
	// there's a save, else the title screen.
	exitRequested = false;
	if (!NewSession()) { quitApp = true; return; }
	open = true;
	if (!startGame) return;
	sys::trace("launch: save check");
	bool have = save::Exists();
	sys::trace("launch: save %s", have ? "found" : "none");
	if (have) StartGame();
	else OpenTitle();
	sys::trace("launch: done");
}

void OneshotWindow::ResetForBoot() {
	if (title.IsOpen()) title.ForceClose();
	exitRequested = false;
	if (!NewSession()) quitApp = true;
	session->menus.titleOpen = false;
	open = false;
}

void OneshotWindow::CloseGame() {
	if (title.IsOpen()) {
		if (desktopMode) ExitGame();
		else quitApp = true;
	} else if (!session->map.IsInScript()) {
		session->map.StartCommonEvent(35);   // QUIT GAME: "Quit and record your progress?"
	} else if (!modal::is_open()) {
		modal::show(modal::Type::Info, "oneshot_window_no_close_during_cutscene", true);
	}
}

void OneshotWindow::Update() {
	if (exitRequested) {
		exitRequested = false;
		if (desktopMode) {
			// OneshotWindow.ExitGame: music off, the game's textures freed, the
			// window closes.
			sys::log("exit game: the OneShot window closes\n");
			twm::restore_wallpaper();
			if (title.IsOpen()) title.ForceClose();
			if (!NewSession()) { quitApp = true; return; }
			session->menus.titleOpen = false;
			gm::clear_cache(gm::Game);
			open = false;
			if (onClosed) onClosed();
			return;
		}
		sys::log("exit game: back to the title screen\n");
		if (!NewSession()) { quitApp = true; return; }
		OpenTitle();
	}
	Session &s = *session;
	if (!s.map.LoadError().empty()) {
		// A map file is missing or damaged: say so instead of running a
		// half-loaded map, and go back to the title screen on a button press.
		if (input::pressed(input::BTN_OK) || input::pressed(input::BTN_CANCEL)) ExitGame();
		return;
	}
	s.state.play_frames++;
	font::tick();
	s.menus.titleOpen = title.IsOpen();
	s.menus.Update();
	s.menus.itemMenu.UpdateSelectedItemIcon();
	if (glitchTimer > 0) {
		glitchTimer--;
		if (++glitchCycle >= 5) { glitchCycle = 0; GenerateGlitch(); }
	}
	UpdateBorder();
	prof::mark("menus update");
	if (!title.IsOpen()) {
		s.map.Update();
		UpdateWindowEffects();
		prof::mark("map update");
		s.map.EventRunnerUpdate();
		prof::mark("events");
		pictures().Update();
		flash().Update();
	} else {
		title.Update();
		prof::mark("title update");
	}
}

void OneshotWindow::Draw() {
	Session &s = *session;
	if (!s.map.LoadError().empty()) {
		gm::color_box(Rect(0, 0, 320, 240), 0, 0, 0, 255);
		font::draw_centered(font::Game, Vec2(320, 150), "The game data could not be loaded:", 0xffffff, 1.0f, 1);
		font::draw_centered(font::Game, Vec2(320, 190), s.map.LoadError(), 0xff8080, 1.0f, 1);
		font::draw_centered(font::Game, Vec2(320, 250), "Copy your game's content and gamedata folders again", 0xffffff, 1.0f, 1);
		font::draw_centered(font::Game, Vec2(320, 280), "to /dev_hdd0/game/OSWM00001/USRDIR/", 0xffffff, 1.0f, 1);
		font::draw_centered(font::Game, Vec2(320, 340), "Press @OK to return to the title screen.", 0xffffff, 1.0f, 1);
		return;
	}
	if (!title.IsOpen()) {
		s.map.Draw();
		prof::mark("map draw");
		s.menus.itemMenu.DrawSelectedItemIcon();
		s.menus.Draw();
		prof::mark("menus draw");
		pictures().Draw();
		if (film.active) {   // FilmPuzzleManager.Draw: what's left of the number sheet
			Rect src(film.left * 2, film.up * 2, (film.right - film.left) * 2, (film.down - film.up) * 2);
			if (src.w > 0 && src.h > 0)
				gm::blit(gm::texture("pictures/numbersheet"), Vec2(film.left * 2, film.up * 2), src, 1.0f, 0, gfx::Blend::Normal, 1);
		}
		flash().Draw();
		s.map.DrawEventRunners();
		prof::mark("events draw");
		s.map.DrawMapTransition();
	} else {
		title.Draw();
		s.menus.Draw();
		prof::mark("title draw");
		if (!dataWarning.empty()) font::draw(font::Game, Vec2(16, 448), dataWarning, 0xff8080, 1.0f, 1);
	}
}

// ===================================================================== window effects

void OneshotWindow::Shake(int frames) {
	shake.timer = 0;
	shake.total = frames;
	shake.start = twm::game_window_pos();
}

void OneshotWindow::FilmPuzzleBegin() {
	film.left = film.up = 0;
	film.right = 320;
	film.down = 240;
	film.active = true;
}

bool OneshotWindow::FilmPuzzleCleared() const {
	return film.right - film.left <= 0 || film.down - film.up <= 0;
}

void OneshotWindow::UpdateWindowEffects() {
	if (film.active) {
		// The parts of the picture that have been off the screen are gone.
		Vec2 p = twm::game_window_pos() + Vec2(2, 26), sz = twm::screen_size();
		if (-p.x > film.left) film.left = -p.x;
		if (-p.y > film.up) film.up = -p.y;
		if (sz.x - p.x < film.right) film.right = sz.x - p.x;
		if (sz.y - 30 - p.y < film.down) film.down = sz.y - 30 - p.y;
	}
	if (shake.total > 0) {
		if (++shake.timer >= shake.total) {
			twm::set_game_window_pos(shake.start);
			shake.timer = shake.total = 0;
		} else {
			int n = (shake.total - shake.timer) / 2;
			twm::set_game_window_pos(Vec2(shake.start.x + util::random(-n, n), shake.start.y + util::random(-n, n)));
		}
	}
}

static float frand(float a, float b) { return a + (b - a) * (util::random(0, 1000000) / 1000000.0f); }

// MathHelper.ApplyHue
static void apply_hue(float c[3], float h) {
	const float k = 0.57735f;
	float cs = cosf(h), sn = sinf(h), d = k * (c[0] + c[1] + c[2]) * (1 - cs);
	float x = c[0], y = c[1], z = c[2];
	float cx = k * z - k * y, cy = k * x - k * z, cz = k * y - k * x;   // cross((k,k,k), c)
	c[0] = x * cs + cx * sn + k * d;
	c[1] = y * cs + cy * sn + k * d;
	c[2] = z * cs + cz * sn + k * d;
	for (int i = 0; i < 3; i++) c[i] = c[i] < 0 ? 0 : c[i] > 1 ? 1 : c[i];
}

void OneshotWindow::StartGlitch(int frames, float strength) {
	if (!settings().screenTear) return;
	glitchTimer = frames;
	glitchCycle = 0;
	glitchStrength = strength;
	GenerateGlitch();
}

void OneshotWindow::GenerateGlitch() {
	glitch.clear();
	for (int left = 480; left > 0;) {
		gm::GlitchSegment g;
		g.shift = (int)((util::random(-160, 160) * 2) * glitchStrength);
		if (g.shift < 0) g.shift += 640;
		g.height = std::min(util::random(15, 120) * 2, left);
		float c[3] = { 1.0f, frand(1.0f - 0.5f * glitchStrength, 1.0f), 1.0f };
		apply_hue(c, frand(0, 6.2831853f));
		g.r = (uint8_t)(c[0] * 255);
		g.g = (uint8_t)(c[1] * 255);
		g.b = (uint8_t)(c[2] * 255);
		glitch.push_back(g);
		left -= g.height;
	}
}

uint32_t OneshotWindow::Border::For(int id) const {
	uint32_t first = 0x8541d1;
	for (auto &c : colors) {
		if (c.first == id) return c.second;
		if (c.first == 1) first = c.second;
	}
	return first;
}

static int approach(int target, int start, float ratio) {   // MathHelper.ApproachInt
	int d = target - start, step = (int)(d * ratio);
	if (step == 0) step = d >= 0 ? 1 : -1;
	if (d == 0) return target;
	return start + step;
}

void OneshotWindow::UpdateBorder() {
	if (!border.loaded) {
		border.loaded = true;
		if (cJSON *j = parse_json_file("gamedata/oneshot_map_colors.json")) {
			const cJSON *it;
			cJSON_ArrayForEach(it, cJSON_GetObjectItemCaseSensitive(j, "mapColors")) {
				const cJSON *c = cJSON_GetObjectItemCaseSensitive(it, "color");
				uint32_t rgb = (uint32_t)cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(c, "r")) << 16 |
				               (uint32_t)cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(c, "g")) << 8 |
				               (uint32_t)cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(c, "b"));
				const cJSON *m;
				cJSON_ArrayForEach(m, cJSON_GetObjectItemCaseSensitive(it, "maps")) {
					bool found = false;
					for (auto &e : border.colors)
						if (e.first == (int)m->valuedouble) { e.second = rgb; found = true; }
					if (!found) border.colors.push_back({ (int)m->valuedouble, rgb });
				}
			}
			cJSON_Delete(j);
		} else {
			sys::log("gamedata/oneshot_map_colors.json missing: the fullscreen border stays purple\n");
		}
	}
	int id = session->map.GetMapID();
	if (border.mapId < 0) {
		border.mapId = id;
		border.current = border.For(id);
	}
	if (border.mapId != id) {
		border.mapId = id;
		border.next = border.For(id);
		if (border.current != border.next) {
			border.prev = border.current;
			border.timer = 0;
			border.shifting = true;
		}
	}
	if (border.shifting) {
		if (++border.timer >= 120) {
			border.current = border.next;
			border.shifting = false;
		} else {
			float f = border.timer / 120.0f;
			auto ch = [&](int sh) { return (uint32_t)approach((int)(border.next >> sh & 255), (int)(border.prev >> sh & 255), f) << sh; };
			border.current = ch(16) | ch(8) | ch(0);
		}
	}
}

void OneshotWindow::ShiftBorderColorInstantly() {
	int id = session->map.GetMapID();
	if (border.mapId != id) {
		border.mapId = id;
		border.current = border.For(id);
	}
	if (border.shifting) {
		border.current = border.next;
		border.shifting = false;
	}
}
