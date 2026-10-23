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
#include "../engine/gm.h"
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

	// The parts of the game that act on its World Machine window.
	void Shake(int frames);                      // WindowShakeManager ("Oneshot.shake")
	void FilmPuzzleBegin();                      // FilmPuzzleManager
	void FilmPuzzleEnd() { film.active = false; }
	bool FilmPuzzleCleared() const;
	void StartGlitch(int frames, float strength = 1.0f);   // GlitchEffectManager
	// The glitch bands for this frame's picture, or nullptr.
	const std::vector<gm::GlitchSegment> *GlitchSegments() const { return glitchTimer > 0 ? &glitch : nullptr; }
	uint32_t BorderColor() const { return border.current; }   // FullscreenBorderManager (0xRRGGBB)
	void ShiftBorderColorInstantly();
	bool CloverEquipped() const { return session && session->state.var(1) == 58; }

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
	struct { bool active = false; int left = 0, up = 0, right = 320, down = 240; } film;
	struct { int total = 0, timer = 0; Vec2 start; } shake;
	std::vector<gm::GlitchSegment> glitch;
	int glitchTimer = 0, glitchCycle = 0;
	float glitchStrength = 1.0f;
	void GenerateGlitch();
	struct Border {
		std::vector<std::pair<int, uint32_t>> colors;   // map id -> colour
		bool loaded = false, shifting = false;
		int mapId = -1, timer = 0;
		uint32_t current = 0x000000, prev = 0, next = 0;
		uint32_t For(int id) const;
	} border;
	void UpdateBorder();
	void UpdateWindowEffects();   // film puzzle and window shake
	std::string dataWarning;
};

OneshotWindow &window();
