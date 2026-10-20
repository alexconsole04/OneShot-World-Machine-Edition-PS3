// panorama.h - Panorama and Fog (OneShotMG.src.Map): the parallax sky
// behind map holes/windows and scrolling fog overlays.
#pragma once
#include "../engine/types.h"
#include "../platform/gfx.h"
#include <string>

class Panorama {
public:
	Panorama(const std::string &imageName, Vec2 size);
	void Update();
	void Draw(Vec2 camPos, Vec2 playerPos, Vec2 mapSize, Vec2 tileSize, GameTone tone) const;
private:
	enum class Type { Normal, Clamped, AnimatedWater, Scroll };
	std::string source, back, front;
	Vec2 size, scrollOffset;
	int frontAlpha = 0, frameIndex = 0, offset_y = 0;
	Type type = Type::Normal;
};

class Fog {
public:
	Fog(const std::string &imageName, Vec2 size, int hue, float opacity, gfx::Blend blend, int scrollX, int scrollY);
	void Update();
	void Draw(Vec2 camPos, GameTone tone) const;
private:
	std::string source;
	Vec2 size;
	int hue;
	float opacity;
	gfx::Blend blend;
	float scrollX, scrollY, offsetX = 0, offsetY = 0;
};
