#include "ui.h"
#include "gm.h"
#include "font.h"
#include "overlay.h"

namespace ui {

void window_border(Rect b, gfx::Blend blend, float alpha) {
	const gfx::Texture *t = gm::texture("ui/window_frame");
	if (!t) {   // fallback when the frame art is missing
		uint8_t a = (uint8_t)(alpha * 255);
		gm::color_box(Rect(b.x, b.y, b.w, 2), 200, 160, 60, a, blend);
		gm::color_box(Rect(b.x, b.y + b.h - 2, b.w, 2), 200, 160, 60, a, blend);
		gm::color_box(Rect(b.x, b.y, 2, b.h), 200, 160, 60, a, blend);
		gm::color_box(Rect(b.x + b.w - 2, b.y, 2, b.h), 200, 160, 60, a, blend);
		return;
	}
	auto mn = [](int a, int c) { return a < c ? a : c; };
	int w = mn(8, b.w / 2), w2 = mn(8, (b.w + 1) / 2);
	int h = mn(8, b.h / 2), h2 = mn(8, (b.h + 1) / 2);
	gm::blit(t, Vec2(b.x, b.y), Rect(0, 0, w, h), alpha, 0, blend);
	gm::blit(t, Vec2(b.x, b.y + b.h - h2), Rect(0, 24 - h2, w, h2), alpha, 0, blend);
	gm::blit(t, Vec2(b.x + b.w - w2, b.y), Rect(24 - w2, 0, w2, h), alpha, 0, blend);
	gm::blit(t, Vec2(b.x + b.w - w2, b.y + b.h - h2), Rect(24 - w2, 24 - h2, w2, h2), alpha, 0, blend);
	for (int i = b.x + 8; i < b.x + b.w - 8; i += 8) {
		int ww = b.x + b.w - 8 - i < 8 ? b.x + b.w - 8 - i : 8;
		gm::blit(t, Vec2(i, b.y), Rect(8, 0, ww, 8), alpha, 0, blend);
		gm::blit(t, Vec2(i, b.y + b.h - 8), Rect(8, 16, ww, 8), alpha, 0, blend);
	}
	for (int j = b.y + 8; j < b.y + b.h - 8; j += 8) {
		int hh = b.y + b.h - 8 - j < 8 ? b.y + b.h - 8 - j : 8;
		gm::blit(t, Vec2(b.x, j), Rect(0, 8, 8, hh), alpha, 0, blend);
		gm::blit(t, Vec2(b.x + b.w - 8, j), Rect(16, 8, 8, hh), alpha, 0, blend);
	}
}

// Game font at scale 1 (window pixels), like TextBox and the menus.
int text_width(const std::string &s, float size) {
	(void)size;
	return font::width(font::Game, s, true);
}
int line_height(float size) { return (int)(24 * size); }

int text(int wx, int wy, const std::string &s, unsigned rgb, float alpha, Align align, float size) {
	(void)size;
	if (align == Align::Center) return font::draw_centered(font::Game, Vec2(wx, wy), s, rgb, alpha, 1);
	return font::draw(font::Game, Vec2(wx, wy), s, rgb, alpha, 1);
}

} // namespace ui
