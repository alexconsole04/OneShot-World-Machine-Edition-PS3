// name_input.h - NameInputMenu: the game's on-screen letter grid for
// entering the player's name (or a password when flag 91 is set).
// The letter grid follows the language (Japanese romaji / hiragana /
// katakana, Russian, European accented letters, English).
#pragma once
#include "../../engine/types.h"
#include <string>

class EventRunner;
struct GameState;

class NameInputMenu {
public:
	NameInputMenu(EventRunner *owner, GameState *state);
	void Open();
	void Close() { st = 3; opacity = 255; }
	bool IsOpen() const { return st != 0; }
	void Update();
	void Draw();
private:
	void HandleCursorInput();
	void SwitchLetters();
	std::string SwitchLabel() const;
	EventRunner *owner;
	GameState *state;
	int st = 0;   // 0 closed, 1 opening, 2 open, 3 closing
	int opacity = 0, cursorAnimTimer = 0;
	float cursorAlpha = 0.5f;
	Vec2 cursor;
	const char *const *letters = nullptr;
	int gw = 10, gh = 5;
	std::string name;
};
