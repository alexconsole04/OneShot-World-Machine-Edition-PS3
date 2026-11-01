// game_window.h - the OneShot window on the desktop (OneshotWindow): the
// game's picture in a window, or maximized with the area's border art.
#pragma once
#include "window.h"

namespace twm {

class GameWindow : public Window {
public:
	GameWindow();
	void ToggleMaximize() override;
	bool Update(bool consumed) override;
	void Draw(const Theme &th) override;
	void DrawContents(const Theme &th, Vec2 screenPos, int alpha) override;
	bool IsSameContent(const Window *other) const override { return other->Kind() == KIND_GAME; }
	int Kind() const override { return KIND_GAME; }
	// GetFullscreenRect, in desktop draw-space pixels.
	Rect FullscreenRect() const;

private:
	void DrawBorder(Rect fr);
	Vec2 oldMaxPos;
	int borderTimer = 0, borderFrame = 0;
};

} // namespace twm
