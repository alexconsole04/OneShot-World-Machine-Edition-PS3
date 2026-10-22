#include "gm.h"
#include "../platform/sys.h"
#include "../xnb.h"

#include <stdlib.h>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <vector>

namespace gm {

static std::unordered_map<std::string, gfx::Texture *> caches[CACHE_COUNT];
static std::unordered_set<std::string> missing;
static gfx::Texture *white_tex;
static float S = 1.0f;
static float OX = 0, OY = 0;

void init() {
	int sw = gfx::screen_w(), sh = gfx::screen_h();
	float sx = sw / 640.0f, sy = sh / 480.0f;
	S = sx < sy ? sx : sy;
	OX = (sw - 640.0f * S) * 0.5f;
	OY = (sh - 480.0f * S) * 0.5f;
	uint32_t px = 0xffffffffu;
	white_tex = gfx::create_texture(1, 1, &px);
	sys::log("gm: window scale %.3f at (%.0f,%.0f)\n", S, OX, OY);
}

static gfx::Texture *load(const std::string &name) {
	std::string rel = "content/" + name + ".xnb";
	uint8_t *data;
	size_t size;
	if (!sys::read_file(rel.c_str(), &data, &size)) return nullptr;
	xnb_texture xt;
	int r = xnb_parse_texture(data, size, &xt, nullptr);
	free(data);
	if (r != XNB_OK) {
		sys::log("gm: %s: %s (file %u KB, heap %d MB in use)\n", rel.c_str(), xnb_strerror(r), (unsigned)(size / 1024),
		         sys::heap_used_kb() / 1024);
		return nullptr;
	}
	gfx::Texture *t = gfx::create_texture(xt.width, xt.height, xt.argb);
	free(xt.argb);
	return t;
}

const gfx::Texture *texture(const char *name, CacheType cache) {
	std::string key(name);
	auto &c = caches[cache];
	auto it = c.find(key);
	if (it != c.end()) return it->second;
	// Remember failures: retrying a missing file every frame means file
	// system calls every frame, which on the PS3 costs whole frames.
	if (missing.count(key)) return nullptr;
	gfx::Texture *t = load(key);
	if (!t) {
		missing.insert(key);
		sys::log("gm: missing texture '%s'\n", name);
		return nullptr;
	}
	c.emplace(key, t);
	return t;
}

Vec2 texture_size(const char *name, CacheType cache) {
	const gfx::Texture *t = texture(name, cache);
	return t ? Vec2((int)t->w, (int)t->h) : Vec2(0, 0);
}

bool texture_exists(const char *name, CacheType cache) { return texture(name, cache) != nullptr; }

int cache_list(CacheType cache, const char **names, const gfx::Texture **texs, int max) {
	std::vector<std::pair<const std::string *, gfx::Texture *>> v;
	for (auto &kv : caches[cache]) v.push_back({ &kv.first, kv.second });
	std::sort(v.begin(), v.end(), [](const auto &a, const auto &b) { return *a.first < *b.first; });
	int n = 0;
	for (auto &e : v) {
		if (n >= max) break;
		names[n] = e.first->c_str();
		texs[n] = e.second;
		n++;
	}
	return n;
}

void clear_cache(CacheType cache) {
	for (auto &kv : caches[cache]) gfx::destroy_texture(kv.second);
	caches[cache].clear();
}

void blit(const gfx::Texture *t, Vec2 p, Rect src, float alpha, int hue, gfx::Blend blend,
          int scale, GameTone tone, float red, float green, float blue) {
	if (!t) return;
	// C#: pixelPos *= scale; dest size = src * scale; then SpriteBatch.
	float x = (float)(p.x * scale), y = (float)(p.y * scale);
	float w = (float)(src.w * scale), h = (float)(src.h * scale);
	gfx::Shade s;
	s.r = red; s.g = green; s.b = blue; s.a = alpha;
	s.tone_r = tone.r; s.tone_g = tone.g; s.tone_b = tone.b;
	s.hue = (int16_t)hue;
	gfx::draw(t, OX + x * S, OY + y * S, w * S, h * S, src.x, src.y, src.w, src.h, s, blend);
}

void blit(const gfx::Texture *t, Vec2 p, float alpha, int hue, gfx::Blend blend, int scale, GameTone tone) {
	if (!t) return;
	blit(t, p, Rect(0, 0, (int)t->w, (int)t->h), alpha, hue, blend, scale, tone);
}

void blit_scaled(const gfx::Texture *t, Vec2 p, Rect src, float xs, float ys, float alpha, int hue,
                 gfx::Blend blend, GameTone tone, float red, float green, float blue) {
	if (!t) return;
	float w = (float)(int)(src.w * xs), h = (float)(int)(src.h * ys);
	gfx::Shade s;
	s.r = red; s.g = green; s.b = blue; s.a = alpha;
	s.tone_r = tone.r; s.tone_g = tone.g; s.tone_b = tone.b;
	s.hue = (int16_t)hue;
	gfx::draw(t, OX + p.x * S, OY + p.y * S, w * S, h * S, src.x, src.y, src.w, src.h, s, blend);
}

void fill_screen(const gfx::Texture *t, Vec2 pos, Rect src, float alpha, gfx::Blend blend, GameTone tone, int hue, int scale) {
	if (!t || src.w <= 0 || src.h <= 0) return;
	for (int i = pos.x; i < 640 / scale; i += src.w)
		for (int j = pos.y; j < 480 / scale; j += src.h)
			blit(t, Vec2(i, j), src, alpha, hue, blend, scale, tone);
}

void color_box(Rect r, uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha, gfx::Blend blend, int scale) {
	if (r.w <= 0 || r.h <= 0) return;
	gfx::Shade s;
	s.r = red / 255.0f; s.g = green / 255.0f; s.b = blue / 255.0f; s.a = alpha / 255.0f;
	gfx::draw(white_tex, OX + r.x * scale * S, OY + r.y * scale * S, r.w * scale * S, r.h * scale * S, 0, 0, 1, 1, s, blend);
}

static const gfx::Texture *captured;

void capture_window() {
	captured = gfx::capture_screen((int)(OX + 0.5f), (int)(OY + 0.5f), (int)(640.0f * S + 0.5f), (int)(480.0f * S + 0.5f));
}

void draw_captured_window(float alpha) {
	if (!captured) return;
	gfx::Shade s;
	s.a = alpha;
	gfx::draw(captured, OX, OY, 640.0f * S, 480.0f * S, 0, 0, (int)captured->w, (int)captured->h, s, gfx::Blend::Normal);
}

void blit_screen(const gfx::Texture *t, float x, float y, float w, float h, Rect src,
                 const gfx::Shade &s, gfx::Blend blend) {
	gfx::draw(t, x, y, w, h, src.x, src.y, src.w, src.h, s, blend);
}

void clip_to_window() {
	gfx::set_clip((int)(OX + 0.5f), (int)(OY + 0.5f), (int)(640.0f * S + 0.5f), (int)(480.0f * S + 0.5f));
}
void clip_off() { gfx::clip_off(); }

const gfx::Texture *white() { return white_tex; }
float window_scale() { return S; }
Vec2 window_origin() { return Vec2((int)OX, (int)OY); }

} // namespace gm
