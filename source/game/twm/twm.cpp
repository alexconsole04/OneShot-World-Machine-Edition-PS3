#include "twm.h"
#include "window.h"
#include "../../engine/audio.h"
#include "../../engine/gm.h"
#include "../../engine/util.h"
#include <math.h>

namespace twm {

static Vec2 g_space(1280, 720), g_screen(640, 360);
static MouseCursor g_cursor;

Vec2 space() { return g_space; }
Vec2 screen() { return g_screen; }
void set_space(Vec2 d) {
	g_space = d;
	g_screen = Vec2(d.x / 2, d.y / 2);
}

MouseCursor &cursor() { return g_cursor; }

void begin() { gm::begin_space(g_space); }
void end() { gm::end_space(); }

void box(Rect r, Color c) {
	if (c.a == 0) return;
	gm::color_box(r, c.r, c.g, c.b, c.a, gfx::Blend::Normal, 2);
}

static const gfx::Texture *get(const char *name, int cache) {
	return gm::texture(name, cache < 0 ? gm::TheWorldMachine : (gm::CacheType)cache);
}

void tex(const char *name, Vec2 pos, Rect src, Color c, int scale, int cache) {
	const gfx::Texture *t = get(name, cache);
	if (!t || c.a == 0) return;
	if (src.w <= 0) src = Rect(0, 0, (int)t->w, (int)t->h);
	gm::blit(t, pos, src, c.a / 255.0f, 0, gfx::Blend::Normal, scale, GameTone(), c.r / 255.0f, c.g / 255.0f, c.b / 255.0f);
}

void tex_add(const char *name, Vec2 pos, Rect src, Color c, int scale, int cache) {
	const gfx::Texture *t = get(name, cache);
	if (!t || c.a == 0) return;
	if (src.w <= 0) src = Rect(0, 0, (int)t->w, (int)t->h);
	gm::blit(t, pos, src, c.a / 255.0f, 0, gfx::Blend::Additive, scale, GameTone(), c.r / 255.0f, c.g / 255.0f, c.b / 255.0f);
}

Vec2 tex_size(const char *name, int cache) {
	const gfx::Texture *t = get(name, cache);
	return t ? Vec2((int)t->w, (int)t->h) : Vec2(0, 0);
}

static unsigned rgb(Color c) { return (unsigned)(c.r << 16 | c.g << 8 | c.b); }

void text(Vec2 pos, const std::string &s, Color c, font::Type t, font::Mode m) {
	if (c.a == 0 || s.empty()) return;
	font::draw(t, pos, s, rgb(c), c.a / 255.0f, 2, m);
}

void text_centered(Vec2 pos, const std::string &s, Color c, font::Type t) {
	if (c.a == 0 || s.empty()) return;
	font::draw_centered(t, pos, s, rgb(c), c.a / 255.0f, 2);
}

int text_w(const std::string &s, font::Type t) { return font::width(t, s); }

void line(Vec2 pos, const std::string &s, Color c, int maxWidth, bool xCentered) {
	if (c.a == 0 || s.empty()) return;
	int w = font::width(font::OS, s, true);
	if (w % 2 == 1) w--;
	float texW = (float)(w + 4) * 2;   // texture pixels (desktop draw space)
	float sq = 1.0f;
	if (maxWidth > 0 && texW > maxWidth * 2) {
		sq = maxWidth * 2 / texW;
		texW = (float)(maxWidth * 2);
	}
	float px = pos.x * 2.0f - (xCentered ? texW / 2 : 0);
	float py = pos.y * 2.0f;
	// The text sits 2 right and 4 up (desktop pixels) in its texture.
	if (sq != 1.0f) font::set_squeeze(sq, px);
	font::draw(font::OS, Vec2((int)floorf((px + 4 * sq) / 2), (int)((py - 8) / 2)), s, rgb(c), c.a / 255.0f, 2, font::Mode::OnlyText);
	if (sq != 1.0f) font::set_squeeze(1.0f, 0);
}

std::string truncate(const std::string &s, int maxLen, font::Type t) {
	if (font::width(t, s) < maxLen) return s;
	std::vector<size_t> starts;
	for (size_t i = 0; i < s.size(); i += font::utf8_len((unsigned char)s[i])) starts.push_back(i);
	for (size_t n = starts.size(); n > 0; n--) {
		std::string r = s.substr(0, starts[n - 1]) + "..";
		if (font::width(t, r) <= maxLen) return r;
	}
	return "";
}

std::vector<std::string> wrap(const std::string &s, int width, font::Type t) { return font::word_wrap(t, s, width); }

int ease_out(int start, int target, int t, int total) {
	float f = (float)t / total;
	return start + (int)lroundf((target - start) * f * f * f);
}

void play_sound(const char *name, float vol, float pitch) { audio::play_sound(name, vol, pitch); }

static std::vector<Window *> g_deletes;
void defer_delete(Window *w) { if (w) g_deletes.push_back(w); }
void flush_deletes() {
	std::vector<Window *> d;
	d.swap(g_deletes);
	for (Window *w : d) delete w;
}

} // namespace twm
