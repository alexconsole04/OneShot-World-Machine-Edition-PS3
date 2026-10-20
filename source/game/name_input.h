// name_input.h - NameInputMenu: the game's on-screen letter grid for
// entering the player's name (or a password when flag 91 is set).
// English layout (upper/lower case), as the PS3 build ships English only.
#pragma once
#include "../engine/types.h"
#include <string>

class EventRunner;
struct GameState;

class NameInputMenu {
public:
	NameInputMenu(EventRunner *owner, GameState *state) : owner(owner), state(state) {}
	void Open();
	void Close() { st = 3; opacity = 255; }
	bool IsOpen() const { return st != 0; }
	void Update();
	void Draw();
private:
	void HandleCursorInput();
	EventRunner *owner;
	GameState *state;
	int st = 0;   // 0 closed, 1 opening, 2 open, 3 closing
	int opacity = 0, cursorAnimTimer = 0;
	float cursorAlpha = 0.5f;
	Vec2 cursor;
	bool lower = false;
	std::string name;
};
