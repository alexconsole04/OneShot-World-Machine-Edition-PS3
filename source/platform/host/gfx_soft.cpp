// gfx_soft.cpp - host software renderer. Same semantics as the RSX path
// (nearest sampling, straight-alpha / additive blending, shade_math.h),
// used to render verification screenshots without a PS3.
#include "../gfx.h"
#include "../shade_math.h"
#include "../sys.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <vector>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

namespace gfx {

static const int W = 1280, H = 720;
static std::vector<uint32_t> fb;     // 0x00RRGGBB
static std::vector<uint32_t> shown;  // last completed frame
static Stats st, cur;
static uint32_t tex_used;
static int clip_x0 = 0, clip_y0 = 0, clip_x1 = W, clip_y1 = H;

bool init() {
	fb.assign(W * H, 0);
	shown.assign(W * H, 0);
	return true;
}
void shutdown() {}
int screen_w() { return W; }
int screen_h() { return H; }

Texture *create_texture(uint32_t w, uint32_t h, const uint32_t *argb) {
	Texture *t = new Texture;
	t->w = w; t->h = h; t->pitch = w * 4; t->bytes = w * h * 4;
	t->mem = malloc(t->bytes);
	memcpy(t->mem, argb, t->bytes);
	tex_used += t->bytes;
	return t;
}
void destroy_texture(Texture *t) {
	if (!t) return;
	tex_used -= t->bytes;
	free(t->mem);
	delete t;
}

void begin_frame(uint32_t clear_argb) {
	for (auto &p : fb) p = clear_argb & 0xffffff;
	clip_off();
	cur = Stats{};
	cur.draw_calls = 0;
}

static const Texture *batch_tex;
static Blend batch_blend;

void draw(const Texture *t, float dx, float dy, float dw, float dh,
          int sx, int sy, int sw, int sh, const Shade &s, Blend blend) {
	if (!t || dw <= 0 || dh <= 0) return;
	cur.quads++;
	if (t != batch_tex || blend != batch_blend || cur.draw_calls == 0) {
		cur.draw_calls++;
		batch_tex = t; batch_blend = blend;
	}
	// Pixel centres inside [dx, dx+dw) x [dy, dy+dh), like the rasteriser.
	int x0 = (int)ceilf(dx - 0.5f), x1 = (int)ceilf(dx + dw - 0.5f);
	int y0 = (int)ceilf(dy - 0.5f), y1 = (int)ceilf(dy + dh - 0.5f);
	if (x0 < clip_x0) x0 = clip_x0;
	if (y0 < clip_y0) y0 = clip_y0;
	if (x1 > clip_x1) x1 = clip_x1;
	if (y1 > clip_y1) y1 = clip_y1;
	const uint32_t *px = (const uint32_t *)t->mem;
	const float hue = s.hue * (3.14159265f / 180.0f);
	const float tr = s.tone_r / 255.0f, tg = s.tone_g / 255.0f, tb = s.tone_b / 255.0f;
	for (int y = y0; y < y1; y++) {
		float v = ((y + 0.5f) - dy) / dh;
		int ty = sy + (int)floorf(v * sh);
		ty = ((ty % (int)t->h) + (int)t->h) % (int)t->h;          // wrap (PointWrap)
		for (int x = x0; x < x1; x++) {
			float u = ((x + 0.5f) - dx) / dw;
			int tx = sx + (int)floorf(u * sw);
			tx = ((tx % (int)t->w) + (int)t->w) % (int)t->w;
			uint32_t p = px[ty * t->w + tx];
			shade::RGBA c = { ((p >> 16) & 255) / 255.0f, ((p >> 8) & 255) / 255.0f,
			                  (p & 255) / 255.0f, (p >> 24) / 255.0f };
			c = shade::apply(c, s.r, s.g, s.b, s.a, tr, tg, tb, hue);
			if (c.a <= 0.0f) continue;
			uint32_t &d = fb[y * W + x];
			float dr = ((d >> 16) & 255) / 255.0f, dg = ((d >> 8) & 255) / 255.0f, db = (d & 255) / 255.0f;
			if (blend == Blend::Additive) {
				dr = shade::clamp01(dr + c.r * c.a); dg = shade::clamp01(dg + c.g * c.a); db = shade::clamp01(db + c.b * c.a);
			} else {
				dr = c.r * c.a + dr * (1 - c.a); dg = c.g * c.a + dg * (1 - c.a); db = c.b * c.a + db * (1 - c.a);
			}
			d = ((uint32_t)(dr * 255 + 0.5f) << 16) | ((uint32_t)(dg * 255 + 0.5f) << 8) | (uint32_t)(db * 255 + 0.5f);
		}
	}
}

void end_frame() {
	shown = fb;
	st = cur;
	st.tex_bytes_used = tex_used;
	st.tex_bytes_total = 0;
	batch_tex = nullptr;
}

void set_clip(int x, int y, int w, int h) {
	clip_x0 = x < 0 ? 0 : x; clip_y0 = y < 0 ? 0 : y;
	clip_x1 = x + w > W ? W : x + w; clip_y1 = y + h > H ? H : y + h;
}
void clip_off() { clip_x0 = 0; clip_y0 = 0; clip_x1 = W; clip_y1 = H; }

Stats last_stats() { return st; }

bool screenshot(const char *path) {
	std::vector<uint8_t> rgb(W * H * 3);
	for (int i = 0; i < W * H; i++) {
		rgb[i * 3 + 0] = (shown[i] >> 16) & 255;
		rgb[i * 3 + 1] = (shown[i] >> 8) & 255;
		rgb[i * 3 + 2] = shown[i] & 255;
	}
	int ok = stbi_write_png(path, W, H, 3, rgb.data(), W * 3);
	sys::log("screenshot %s: %s\n", path, ok ? "ok" : "FAILED");
	return ok != 0;
}

} // namespace gfx
