// hintbox.h - HintBox: the desktop tutorials' text box, which floats near
// what it's pointing at (the cursor, an icon, a window's button).
#pragma once
#include "twm.h"
#include <functional>
#include <string>
#include <vector>

namespace twm {

class HintBox {
public:
	HintBox(Vec2 screenCenter, const std::string &text);
	std::function<Vec2()> GetTargetPos;
	std::function<void()> OnFadeOutComplete;
	bool FadeOut = false;
	// True on the frame the fade out finishes: the caller then runs
	// OnFadeOutComplete (which usually replaces this box).
	bool Update();
	void Draw(const Theme &th) const;
	bool IsFadeInComplete() const { return alpha == 255 || FadeOut; }

private:
	Vec2 TargetQuadrant(Vec2 target);
	Vec2 screenCenter, targetPos, boxSize, lastQuadrant;
	float offX = 0, offY = 0;
	int driftTimer = 0, alpha = 0;
	std::vector<std::string> lines;
};

} // namespace twm
