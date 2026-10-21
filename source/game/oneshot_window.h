// oneshot_window.h - OneshotWindow: owns one play session (game state, map,
// menus) plus the title screen, and runs GameLogic / PreDraw in the PC
// build's order.
//
// On the PC, "ExitGame" closes the game window and leaves the player on
// the World Machine desktop. Here it returns to the title screen with a
// fresh session (the save and permanent flags are re-read), and "Exit" on
// the title screen quits to the XMB.
#pragma once
#include "game_state.h"
#include "menus.h"
#include "tilemap.h"
#include "title_screen.h"
#include <memory>

class OneshotWindow {
public:
	// Creates the first session. False if the game data can't be read.
	bool Boot(bool showTitle);
	void Update();   // GameLogic
	void Draw();     // PreDraw
	void StartGame();          // a new game or, if a save exists, continue it
	void ExitGame();           // back to the title screen (takes effect next frame)
	void CloseGame();          // the window's close button / the Quit menu entry
	bool QuitRequested() const { return quitApp; }
	bool TitleOpen() const { return title.IsOpen(); }

	GameState &State() { return session->state; }
	TileMap &Map() { return session->map; }
	MenuManager &Menus() { return session->menus; }

private:
	struct Session {
		GameState state;
		TileMap map;
		MenuManager menus;
		Session() : map(&state), menus(&map, &state) {}
	};
	bool NewSession();
	void OpenTitle();
	std::unique_ptr<Session> session;
	TitleScreen title;
	bool exitRequested = false, quitApp = false;
};

OneshotWindow &window();
