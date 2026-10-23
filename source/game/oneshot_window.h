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
#include <functional>
#include <memory>
#include <string>

class OneshotWindow {
public:
	// Creates the first session. False if the game data can't be read.
	bool Boot(bool showTitle);
	void Update();   // GameLogic
	void Draw();     // PreDraw
	void StartGame();          // a new game or, if a save exists, continue it
	void ExitGame();           // back to the title screen (takes effect next frame)
	void CloseGame();          // the window's close button / the Quit menu entry
	// Debug map switcher: leaves the title screen if needed, clears pictures
	// and flashes, stops all scripts, then loads the map.
	bool DebugTeleport(int mapId, int tileX, int tileY, Direction facing);
	bool QuitRequested() const { return quitApp; }
	void ShowTitle() { OpenTitle(); }   // after the boot sequence (no desktop)
	// The desktop's OneShot icon: a fresh session, then the save or the
	// title screen (startGame false: an empty session for the map switcher).
	void Launch(bool startGame);
	bool IsOpen() const { return open; }
	// Desktop mode: "exit" closes the window instead of returning to the
	// title screen, and the title's Exit goes back to the desktop.
	bool desktopMode = false;
	std::function<void()> onClosed;          // the window closed itself (ExitGame)
	std::function<void()> onOpenedForDebug;  // the map switcher opened it
	// A language change: drop the session (its textures and text) before the
	// boot console runs again; the title screen opens when it's done.
	void ResetForBoot();
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
	bool exitRequested = false, quitApp = false, open = false;
	std::string dataWarning;
};

OneshotWindow &window();
