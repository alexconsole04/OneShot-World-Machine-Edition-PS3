#include "gm.h"
#include "../platform/sys.h"
#include "../xnb.h"

#include <stdlib.h>
#include <string>
#include <unordered_map>
#include <unordered_set>

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
		sys::log("gm: %s: %s\n", rel.c_str(), xnb_strerror(r));
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
	gfx::Texture *t = load(key);
	if (!t) {
		if (missing.insert(key).second) sys::log("gm: missing texture '%s'\n", name);
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

void blit_screen(const gfx::Texture *t, float x, float y, float w, float h, Rect src,
                 const gfx::Shade &s, gfx::Blend blend) {
	gfx::draw(t, x, y, w, h, src.x, src.y, src.w, src.h, s, blend);
}

const gfx::Texture *white() { return white_tex; }
float window_scale() { return S; }
Vec2 window_origin() { return Vec2((int)OX, (int)OY); }

} // namespace gm
