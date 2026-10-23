#include "gm.h"
#include "../platform/sys.h"
#include "../xnb.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <vector>

namespace gm {

struct CacheEntry { gfx::Texture *t; int gen; };
static std::unordered_map<std::string, CacheEntry> caches[CACHE_COUNT];
static int generation[CACHE_COUNT];
static std::unordered_set<std::string> missing;
static gfx::Texture *white_tex;
static float S = 1.0f;
static float OX = 0, OY = 0;
static float S0 = 1.0f, OX0 = 0, OY0 = 0;   // the window's place on the TV
// Offscreen window (see begin_window).
static gfx::Texture *win_rt;
static int win_k, scaling_mode, rt_failed_k;
static bool in_window, last_frame_rt, in_space;

void init() {
	int sw = gfx::screen_w(), sh = gfx::screen_h();
	float sx = sw / 640.0f, sy = sh / 480.0f;
	S = sx < sy ? sx : sy;
	OX = (sw - 640.0f * S) * 0.5f;
	OY = (sh - 480.0f * S) * 0.5f;
	S0 = S; OX0 = OX; OY0 = OY;
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

bool pixel_solid(const char *name, Vec2 p, CacheType cache) {
	const gfx::Texture *t = texture(name, cache);
	if (!t || !t->mem || p.x < 0 || p.y < 0 || p.x >= (int)t->w || p.y >= (int)t->h) return false;
	// Textures are stored as rows of 32-bit ARGB words (in VRAM on the PS3,
	// which the PPU can read: slow, but this is one texel).
	uint32_t px = ((const volatile uint32_t *)((const uint8_t *)t->mem + (size_t)p.y * t->pitch))[p.x];
	return (px >> 24) > 128;
}

static std::string (*g_redirect)(const std::string &);
void set_texture_redirect(std::string (*fn)(const std::string &)) { g_redirect = fn; }

const gfx::Texture *texture(const char *name, CacheType cache) {
	std::string key(name);
	auto &c = caches[cache];
	auto it = c.find(key);
	if (it != c.end()) {
		it->second.gen = generation[cache];
		return it->second.t;
	}
	// Remember failures: retrying a missing file every frame means file
	// system calls every frame, which on the PS3 costs whole frames.
	if (missing.count(key)) return nullptr;
	gfx::Texture *t = nullptr;
	if (g_redirect) {
		// CacheAndManager.GetTexture: the language's own version if it has one.
		std::string local = g_redirect(key);
		if (local != key) {
			t = load(local);
			if (t) sys::log("gm: localized texture %s\n", local.c_str());
		}
	}
	if (!t) t = load(key);
	if (!t) {
		missing.insert(key);
		sys::log("gm: missing texture '%s'\n", name);
		return nullptr;
	}
	c.emplace(key, CacheEntry{ t, generation[cache] });
	return t;
}

Vec2 texture_size(const char *name, CacheType cache) {
	const gfx::Texture *t = texture(name, cache);
	return t ? Vec2((int)t->w, (int)t->h) : Vec2(0, 0);
}

bool texture_exists(const char *name, CacheType cache) { return texture(name, cache) != nullptr; }

int cache_list(CacheType cache, const char **names, const gfx::Texture **texs, int max) {
	std::vector<std::pair<const std::string *, gfx::Texture *>> v;
	for (auto &kv : caches[cache]) v.push_back({ &kv.first, kv.second.t });
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
	for (auto &kv : caches[cache]) gfx::destroy_texture(kv.second.t);
	caches[cache].clear();
}

void new_generation(CacheType cache, int keep) {
	static const uint32_t BUDGET = 96u * 1024 * 1024;   // VRAM for cached textures before keeping fewer
	int g = ++generation[cache];
	if (gfx::last_stats().tex_bytes_used > BUDGET) keep = 0;
	auto &c = caches[cache];
	int evicted = 0, kept = 0;
	for (auto it = c.begin(); it != c.end();) {
		if (it->second.gen < g - keep) {
			gfx::destroy_texture(it->second.t);
			it = c.erase(it);
			evicted++;
		} else {
			++it;
			kept++;
		}
	}
	sys::log("gm: texture cache: kept %d, released %d\n", kept, evicted);
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
	// The offscreen window still holds the last frame's game picture, without
	// the debug overlay or anything else drawn over it on the TV.
	if (last_frame_rt && win_rt) captured = gfx::capture_texture(win_rt);
	else captured = gfx::capture_screen((int)(OX0 + 0.5f), (int)(OY0 + 0.5f), (int)(640.0f * S0 + 0.5f), (int)(480.0f * S0 + 0.5f));
}

void set_window_placement(float ox, float oy, float scale) {
	S0 = scale; OX0 = ox; OY0 = oy;
	if (!in_window && !in_space) { S = S0; OX = OX0; OY = OY0; }
}

void reset_window_placement() {
	int sw = gfx::screen_w(), sh = gfx::screen_h();
	float sx = sw / 640.0f, sy = sh / 480.0f;
	float sc = sx < sy ? sx : sy;
	set_window_placement((sw - 640.0f * sc) * 0.5f, (sh - 480.0f * sc) * 0.5f, sc);
}

Vec2 space_to_tv(Vec2 size, float x, float y, float *scale) {
	float sx = (float)gfx::screen_w() / size.x, sy = (float)gfx::screen_h() / size.y;
	float k = sx < sy ? sx : sy;
	float ox = (gfx::screen_w() - size.x * k) * 0.5f, oy = (gfx::screen_h() - size.y * k) * 0.5f;
	if (scale) *scale = k;
	return Vec2((int)(ox + x * k + 0.5f), (int)(oy + y * k + 0.5f));
}

void set_scaling(int mode) { scaling_mode = mode == SCALE_SMOOTH ? SCALE_SMOOTH : SCALE_SHARP; }

static const std::vector<GlitchSegment> *g_glitch;
void set_window_glitch(const std::vector<GlitchSegment> *segments) { g_glitch = segments; }

const char *scaling_info() {
	static char buf[64];
	static const char *n[SCALE_COUNT] = { "sharp", "smooth" };
	if (!win_rt) snprintf(buf, sizeof(buf), "%s, straight to the TV at %.2fx", n[scaling_mode], S0);
	else snprintf(buf, sizeof(buf), "%s, drawn at %dx (%ux%u)", n[scaling_mode], win_k, win_rt->w, win_rt->h);
	return buf;
}

// Integer scale the window is drawn at for the current mode.
static int window_k() {
	switch (scaling_mode) {
	case SCALE_SMOOTH: return 1;
	default: {
		// Sharp: the nearest whole-number scale, then filtered the rest of the
		// way: 1080p (2.25x) draws at 2x and stretches by 1.125; 720p (1.5x)
		// draws at 2x and shrinks by 0.75. Never more pixels than drawing
		// straight to the TV would cost at 1080p.
		int f = S0 < 1.0f ? 1 : (int)(S0 + 0.01f);
		return S0 / f > 1.25f ? f + 1 : f;
	}
	}
}

void begin_window() {
	int k = window_k();
	// Exactly k TV pixels per drawn pixel (a 1080p desktop's window at 1x):
	// no filtering.
	bool linear = fabsf(S0 - (float)k) > 0.001f;
	if (!win_rt || win_k != k) {
		gfx::destroy_texture(win_rt);
		win_rt = nullptr;
		win_k = k;
		if (rt_failed_k != k) win_rt = gfx::create_render_target(640 * k, 480 * k);
		if (!win_rt) rt_failed_k = k;   // don't retry every frame
	}
	if (!win_rt) {   // no VRAM: draw straight onto the TV as before
		last_frame_rt = false;
		return;
	}
	win_rt->linear = linear;
	gfx::set_render_target(win_rt);
	S = (float)k;
	OX = OY = 0;
	in_window = true;
	last_frame_rt = true;
	gfx::Shade black;
	black.r = black.g = black.b = 0;
	gfx::draw(white_tex, 0, 0, 640.0f * k, 480.0f * k, 0, 0, 1, 1, black, gfx::Blend::Normal);
}

void end_window() {
	if (!in_window) return;
	in_window = false;
	gfx::set_render_target(nullptr);
	S = S0; OX = OX0; OY = OY0;
	gfx::Shade s;
	if (g_glitch && !g_glitch->empty()) {
		// Bands of the picture, each moved left by its shift and wrapped.
		float k = (float)win_k;
		int y = 0;
		for (const GlitchSegment &g : *g_glitch) {
			if (y >= 480) break;
			int h = std::min(g.height, 480 - y), x = g.shift;
			if (h <= 0) continue;
			gfx::Shade t;
			t.r = g.r / 255.0f; t.g = g.g / 255.0f; t.b = g.b / 255.0f;
			gfx::draw(win_rt, OX, OY + y * S, (640 - x) * S, h * S, (int)(x * k), (int)(y * k), (int)((640 - x) * k),
			          (int)(h * k), t, gfx::Blend::Normal);
			if (x > 0)
				gfx::draw(win_rt, OX + (640 - x) * S, OY + y * S, x * S, h * S, 0, (int)(y * k), (int)(x * k), (int)(h * k), t,
				          gfx::Blend::Normal);
			y += h;
		}
		g_glitch = nullptr;
		return;
	}
	gfx::draw(win_rt, OX, OY, 640.0f * S, 480.0f * S, 0, 0, (int)win_rt->w, (int)win_rt->h, s, gfx::Blend::Normal);
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

// ------------------------------------------------------------------ boot screen / desktop space

Vec2 boot_space() {
	// GraphicsManager.GetBootScreenSize for a TV: 720 pixels on the short side.
	int sw = gfx::screen_w(), sh = gfx::screen_h();
	if (sw * 3 >= sh * 4) return Vec2((int)(sw * 720.0 / sh + 0.5), 720);
	return Vec2(720, (int)(sh * 720.0 / sw + 0.5));
}

static int space_w, space_h;

void begin_space(Vec2 size) {
	// No off-screen pass: at 1080p one space pixel is 1.5 TV pixels, so the
	// boot screen's 2x art lands on whole TV pixels (3 per game pixel).
	space_w = size.x;
	space_h = size.y;
	float sx = (float)gfx::screen_w() / size.x, sy = (float)gfx::screen_h() / size.y;
	S = sx < sy ? sx : sy;
	OX = (gfx::screen_w() - size.x * S) * 0.5f;
	OY = (gfx::screen_h() - size.y * S) * 0.5f;
	in_space = true;
}

void end_space() {
	if (!in_space) return;
	in_space = false;
	S = S0; OX = OX0; OY = OY0;
}

void set_dither(float opacity) {
	// DitherShader parameters: cells of 4 space pixels; the wipe line
	// reaches the bottom (plus a 50-cell band) at opacity 1.
	float h = (float)(space_h > 0 ? space_h : 720);
	float line = opacity * (h / 4.0f + 50.0f) - 50.0f;
	gfx::set_dither(OX, OY, 4.0f * S, line, texture("transitions/BayerDither8x8", BootScreen));
}

void clip_to_window() {
	gfx::set_clip((int)(OX + 0.5f), (int)(OY + 0.5f), (int)(640.0f * S + 0.5f), (int)(480.0f * S + 0.5f));
}
void clip_off() { gfx::clip_off(); }

const gfx::Texture *white() { return white_tex; }
float window_scale() { return S; }
Vec2 window_origin() { return Vec2((int)OX, (int)OY); }

} // namespace gm
