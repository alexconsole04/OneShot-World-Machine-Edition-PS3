// ui.h - shared UI drawing: TextBox.DrawWindowBorder and text in the
// game's 640x480 window space. Text is the Game SpriteFont (font.h) at
// window scale.
#pragma once
#include "types.h"
#include "../platform/gfx.h"
#include <string>

namespace ui {
// Rect in game pixels (320x240), like the C#.
void window_border(Rect box, gfx::Blend blend, float alpha);
// Window-pixel position (640x480). Returns the drawn width in window px.
enum class Align { Left, Center };
int text(int wx, int wy, const std::string &s, unsigned rgb = 0xffffff, float alpha = 1.0f,
         Align align = Align::Left, float size = 1.0f);
int text_width(const std::string &s, float size = 1.0f);
int line_height(float size = 1.0f);
}
