// mouse_cursor.h - port of MouseCursorManager: the World Machine desktop's
// cursor, moved with the right stick (the console versions' scheme) and
// clicked with R2. Positions are in desktop pixels: half the desktop's
// draw size (the desktop draws at 2x; 640x360 on a 16:9 TV).
#pragma once
#include "../engine/types.h"

class MouseCursor {
public:
	enum State { Normal = 0, Clickable = 1, Grabbable = 3, Holding = 4, Move = 5, NotAllowed = 6 };
	void Update();
	void Draw() const;
	void SetState(State s) { if (state != Holding) state = s; }
	void SetMousePos(Vec2 p) { pos = p; }
	Vec2 MousePos() const { return pos; }
	bool Clicked() const { return clicked; }
	bool Held() const { return held; }
	bool hidden = false;
	int speed = 5;          // InputManager.MouseCursorSpeed (settings: 1..10)
	int deadZone = 20;      // InputManager.MouseStickDeadZone (PC default 10; DualShock 3 sticks need more)
	// Debug cursor test: keep this state instead of resetting to Normal each frame.
	State forcedState = Normal;
	bool forceState = false;
private:
	Vec2 pos;
	State state = Normal;
	bool clicked = false, held = false;
	Rect frameRect = Rect(0, 0, 16, 16);
	int frameIndex = 0, frameTimer = 0;
	Vec2 offset;
};

// Desktop space size (DrawScreenSize / 2): 640x360 on a 16:9 TV.
Vec2 desktop_space();
