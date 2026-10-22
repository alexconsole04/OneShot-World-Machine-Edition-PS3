#include "oneshot_window.h"
#include "loc.h"
#include "modal.h"
#include "pictures.h"
#include "player.h"
#include "save.h"
#include "settings.h"
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
	pictures().Clear();
	flash() = FlashManager();
	audio::stop_all();
	session.reset(new Session());
	Session &s = *session;
	s.state.player_name = sys::user_name();
	if (!s.map.LoadGlobalData()) return false;
	s.menus.itemMenu.LoadData();
	s.map.menus = &s.menus;
	s.menus.mainMenu.onQuit = [this] { CloseGame(); };
	// The constructor of the PC's OneshotWindow: settings, then permanent data.
	settings().Load();
	save::LoadPerma(&s.state);
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
		if (choice == TitleScreen::Exit) quitApp = true;
		else StartGame();
	};
	if (showTitle) OpenTitle();
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

void OneshotWindow::CloseGame() {
	if (title.IsOpen()) {
		quitApp = true;
	} else if (!session->map.IsInScript()) {
		session->map.StartCommonEvent(35);   // QUIT GAME: "Quit and record your progress?"
	} else if (!modal::is_open()) {
		modal::show(modal::Type::Info, loc::twm("oneshot_window_no_close_during_cutscene"));
	}
}

void OneshotWindow::Update() {
	if (exitRequested) {
		exitRequested = false;
		sys::log("exit game: back to the title screen\n");
		if (!NewSession()) { quitApp = true; return; }
		OpenTitle();
	}
	Session &s = *session;
	if (!s.map.LoadError().empty()) {
		// A map file is missing or damaged: say so instead of running a
		// half-loaded map, and go back to the title screen on a button press.
		if (input::pressed(input::BTN_OK) || input::pressed(input::BTN_CANCEL)) ExitGame();
		audio::update();
		return;
	}
	s.state.play_frames++;
	font::tick();
	s.menus.titleOpen = title.IsOpen();
	s.menus.Update();
	s.menus.itemMenu.UpdateSelectedItemIcon();
	prof::mark("menus update");
	if (!title.IsOpen()) {
		s.map.Update();
		prof::mark("map update");
		s.map.EventRunnerUpdate();
		prof::mark("events");
		pictures().Update();
		flash().Update();
	} else {
		title.Update();
		prof::mark("title update");
	}
	audio::update();
	prof::mark("audio update");
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
