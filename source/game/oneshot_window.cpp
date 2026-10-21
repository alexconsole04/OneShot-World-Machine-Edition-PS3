#include "oneshot_window.h"
#include "loc.h"
#include "modal.h"
#include "pictures.h"
#include "player.h"
#include "save.h"
#include "settings.h"
#include "../engine/audio.h"
#include "../engine/font.h"
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
	s.state.play_frames++;
	font::tick();
	s.menus.titleOpen = title.IsOpen();
	s.menus.Update();
	s.menus.itemMenu.UpdateSelectedItemIcon();
	if (!title.IsOpen()) {
		s.map.Update();
		s.map.EventRunnerUpdate();
		pictures().Update();
		flash().Update();
	} else {
		title.Update();
	}
	audio::update();
}

void OneshotWindow::Draw() {
	Session &s = *session;
	if (!title.IsOpen()) {
		s.map.Draw();
		s.menus.itemMenu.DrawSelectedItemIcon();
		s.menus.Draw();
		pictures().Draw();
		flash().Draw();
		s.map.DrawEventRunners();
		s.map.DrawMapTransition();
	} else {
		title.Draw();
		s.menus.Draw();
	}
}
