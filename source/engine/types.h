// types.h - small value types mirroring OneShotMG.Vec2 / Rect / GameTone.
#pragma once
#include <stdint.h>

struct Vec2 {
	int x = 0, y = 0;
	Vec2() = default;
	Vec2(int x_, int y_) : x(x_), y(y_) {}
	Vec2 operator+(const Vec2 &o) const { return { x + o.x, y + o.y }; }
	Vec2 operator-(const Vec2 &o) const { return { x - o.x, y - o.y }; }
	bool operator==(const Vec2 &o) const { return x == o.x && y == o.y; }
	bool operator!=(const Vec2 &o) const { return !(*this == o); }
};

struct Rect {
	int x = 0, y = 0, w = 0, h = 0;
	Rect() = default;
	Rect(int x_, int y_, int w_, int h_) : x(x_), y(y_), w(w_), h(h_) {}
	bool operator==(const Rect &o) const { return x == o.x && y == o.y && w == o.w && h == o.h; }
};

// C# GameTone: shorts, +, * float (truncating casts).
struct GameTone {
	int16_t r = 0, g = 0, b = 0;
	GameTone() = default;
	GameTone(int r_, int g_, int b_) : r((int16_t)r_), g((int16_t)g_), b((int16_t)b_) {}
	GameTone operator+(const GameTone &o) const { return GameTone(r + o.r, g + o.g, b + o.b); }
	GameTone operator*(float f) const { return GameTone((int)(r * f), (int)(g * f), (int)(b * f)); }
	bool operator==(const GameTone &o) const { return r == o.r && g == o.g && b == o.b; }
};

// RPG Maker / Entity.Direction numbering.
enum class Direction : int { None = 0, Down = 2, Left = 4, Right = 6, Up = 8 };

// C# integer division truncates toward zero, same as C++.
static inline int iabs(int v) { return v < 0 ? -v : v; }
