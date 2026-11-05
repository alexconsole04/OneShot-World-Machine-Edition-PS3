// glitch_effect.h - port of GlitchEffect: one picture that now and then
// tears into horizontally shifted, tinted strips (the fast travel minimap
// during the solstice glitch).
#pragma once
#include "gm.h"
#include <vector>

class GlitchEffect {
public:
	// size: the picture's size. Every timeBetween..timeBetween+variance
	// frames it glitches for glitchTime frames, strips shifted by up to
	// `strength` pixels.
	GlitchEffect(Vec2 size, int strength, int timeBetween, int variance, int glitchTime);
	void Update();
	// MainBlit of the whole picture at pos, torn while it's glitching.
	void Draw(const gfx::Texture *t, Vec2 pos, float alpha = 1.0f, float red = 1.0f, float green = 1.0f,
	          float blue = 1.0f, gfx::Blend blend = gfx::Blend::Normal, int scale = 2) const;

private:
	struct Strip { int shift, height; float r, g, b; };
	void Generate();
	Vec2 size;
	int strength, timeBetween, variance, glitchTime;
	int overallTimer = 0, cycleTimer = 0, nextTimer;
	std::vector<Strip> strips;
};
