// gfx_soft.cpp - host software renderer. Same semantics as the RSX path
// (nearest sampling, straight-alpha / additive blending, shade_math.h),
// used to render verification screenshots without a PS3.
#include "../gfx.h"
#include "../shade_math.h"
#include "../sys.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <vector>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

namespace gfx {

static int W = 1280, H = 720;      // OSPS3_SCREEN=1920x1080 to test other TV modes
static std::vector<uint32_t> fb;     // 0x00RRGGBB
static std::vector<uint32_t> shown;  // last completed frame
static Stats st, cur;
static uint32_t tex_used;
static int clip_x0 = 0, clip_y0 = 0, clip_x1 = 1280, clip_y1 = 720;
// What's being drawn into: the frame buffer or a render target's pixels.
static uint32_t *tgt;
static int tw, th;
static Texture *cur_rt;

bool init() {
	if (const char *e = getenv("OSPS3_SCREEN")) sscanf(e, "%dx%d", &W, &H);
	fb.assign(W * H, 0);
	shown.assign(W * H, 0);
	tgt = fb.data(); tw = W; th = H;
	clip_off();
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
	if (t == cur_rt) set_render_target(nullptr);
	tex_used -= t->bytes;
	free(t->mem);
	delete t;
}

void begin_frame(uint32_t clear_argb) {
	set_render_target(nullptr);
	for (auto &p : fb) p = clear_argb & 0xffffff;
	clip_off();
	cur = Stats{};
	cur.draw_calls = 0;
}

static const Texture *batch_tex;
static Blend batch_blend;

static float d_ox, d_oy, d_cell = 4, d_line;
static const Texture *d_bayer;
void set_dither(float ox, float oy, float cell, float line, const Texture *bayer) {
	d_ox = ox; d_oy = oy; d_cell = cell > 0 ? cell : 1; d_line = line; d_bayer = bayer;
}

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
	static const bool dbg_noblend = getenv("OSPS3_NOBLEND") != nullptr;
	static const bool dbg_partial = getenv("OSPS3_PARTIAL") != nullptr;
	const uint32_t *px = (const uint32_t *)t->mem;
	const float hue = s.hue * (3.14159265f / 180.0f);
	const float tr = s.tone_r / 255.0f, tg = s.tone_g / 255.0f, tb = s.tone_b / 255.0f;
	auto texel = [&](int tx, int ty) {
		tx = ((tx % (int)t->w) + (int)t->w) % (int)t->w;              // wrap (PointWrap)
		ty = ((ty % (int)t->h) + (int)t->h) % (int)t->h;
		return px[ty * t->w + tx];
	};
	auto clampi = [](int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); };
	for (int y = y0; y < y1; y++) {
		float v = ((y + 0.5f) - dy) / dh;
		int ty = sy + (int)floorf(v * sh);
		for (int x = x0; x < x1; x++) {
			float u = ((x + 0.5f) - dx) / dw;
			int tx = sx + (int)floorf(u * sw);
			uint32_t p = texel(tx, ty);
			if (t->linear) {
				// Bilinear between the four nearest texel centres (clamped at the edges).
				float fu = sx + u * sw - 0.5f, fv = sy + v * sh - 0.5f;
				int ix = (int)floorf(fu), iy = (int)floorf(fv);
				float ax = fu - ix, ay = fv - iy;
				int xa = clampi(ix, 0, t->w - 1), xb = clampi(ix + 1, 0, t->w - 1);
				int ya = clampi(iy, 0, t->h - 1), yb = clampi(iy + 1, 0, t->h - 1);
				uint32_t q[4] = { px[ya * t->w + xa], px[ya * t->w + xb], px[yb * t->w + xa], px[yb * t->w + xb] };
				uint32_t o = 0;
				for (int sh8 = 0; sh8 < 32; sh8 += 8) {
					float c0 = ((q[0] >> sh8) & 255) * (1 - ax) + ((q[1] >> sh8) & 255) * ax;
					float c1 = ((q[2] >> sh8) & 255) * (1 - ax) + ((q[3] >> sh8) & 255) * ax;
					o |= (uint32_t)(c0 * (1 - ay) + c1 * ay + 0.5f) << sh8;
				}
				p = o;
			}
			shade::RGBA c = { ((p >> 16) & 255) / 255.0f, ((p >> 8) & 255) / 255.0f,
			                  (p & 255) / 255.0f, t->opaque ? 1.0f : (p >> 24) / 255.0f };
			if (blend == Blend::Dither && d_bayer) {
				// dither_fp.fcg
				float cx = floorf((x + 0.5f - d_ox) / d_cell) + 0.5f, cy = floorf((y + 0.5f - d_oy) / d_cell) + 0.5f;
				int mx = ((int)cx % 8 + 8) % 8, my = ((int)cy % 8 + 8) % 8;
				float b = ((((const uint32_t *)d_bayer->mem)[my * d_bayer->w + mx] >> 16) & 255) / 255.0f;
				float k = (d_line - cy) * 0.02f;
				k = k < -1.1f ? -1.1f : k > 0.1f ? 0.1f : k;
				if (k + b < 0.0f) continue;
				c.r *= s.r; c.g *= s.g; c.b *= s.b; c.a = 1.0f;
			} else {
				c = shade::apply(c, s.r, s.g, s.b, s.a, tr, tg, tb, hue);
			}
			if (dbg_noblend) { c.a = 1.0f; blend = Blend::Normal; }   // debug: emulate blending disabled
			if (dbg_partial && c.a > 0.0f) c.a = 1.0f;                // debug: partial alpha -> opaque
			if (c.a <= 0.0f) continue;
			uint32_t &d = tgt[y * tw + x];
			float dr = ((d >> 16) & 255) / 255.0f, dg = ((d >> 8) & 255) / 255.0f, db = (d & 255) / 255.0f;
			if (blend == Blend::Additive) {
				dr = shade::clamp01(dr + c.r * c.a); dg = shade::clamp01(dg + c.g * c.a); db = shade::clamp01(db + c.b * c.a);
			} else if (blend == Blend::Multiply) {
				dr *= c.r; dg *= c.g; db *= c.b;
			} else if (blend == Blend::Brighten) {
				dr = shade::clamp01(dr + dr * c.r); dg = shade::clamp01(dg + dg * c.g); db = shade::clamp01(db + db * c.b);
			} else {
				dr = c.r * c.a + dr * (1 - c.a); dg = c.g * c.a + dg * (1 - c.a); db = c.b * c.a + db * (1 - c.a);
			}
			d = 0xff000000u | ((uint32_t)(dr * 255 + 0.5f) << 16) | ((uint32_t)(dg * 255 + 0.5f) << 8) | (uint32_t)(db * 255 + 0.5f);
		}
	}
}

void end_frame() {
	set_render_target(nullptr);
	for (auto &p : fb) p &= 0xffffffu;
	shown = fb;
	st = cur;
	st.tex_bytes_used = tex_used;
	st.tex_bytes_total = 0;
	batch_tex = nullptr;
}

void set_clip(int x, int y, int w, int h) {
	clip_x0 = x < 0 ? 0 : x; clip_y0 = y < 0 ? 0 : y;
	clip_x1 = x + w > tw ? tw : x + w; clip_y1 = y + h > th ? th : y + h;
}
void clip_off() { clip_x0 = 0; clip_y0 = 0; clip_x1 = tw; clip_y1 = th; }

Texture *create_render_target(uint32_t w, uint32_t h) {
	std::vector<uint32_t> blank(w * h, 0xff000000u);
	Texture *t = create_texture(w, h, blank.data());
	t->opaque = true;
	return t;
}

void set_render_target(Texture *rt) {
	cur_rt = rt;
	if (rt) { tgt = (uint32_t *)rt->mem; tw = (int)rt->w; th = (int)rt->h; }
	else { tgt = fb.data(); tw = W; th = H; }
	clip_off();
}

const Texture *capture_texture(const Texture *rt);

void set_draw_mode(int) {}
int draw_mode() { return 0; }
const char *draw_mode_name() { return "software"; }

void set_plain_shader(bool) {}
bool plain_shader() { return false; }
void set_full_sync(bool) {}
bool full_sync() { return false; }
void set_texel_coords(bool) {}
bool texel_coords() { return false; }

Stats last_stats() { return st; }
void debug_status(char *buf, int n) { if (n > 0) buf[0] = 0; }
void system_menu_closed() {}
void set_system_drawing(bool) {}
void set_present_copy(bool) {}
bool present_copy() { return false; }
static bool vbm = true;
void set_vertex_main_memory(bool on) { vbm = on; }
bool vertex_main_memory() { return vbm; }

static Texture *cap_tex;

const Texture *capture_screen(int x, int y, int w, int h) {
	if (x < 0) { w += x; x = 0; }
	if (y < 0) { h += y; y = 0; }
	if (x + w > W) w = W - x;
	if (y + h > H) h = H - y;
	if (w <= 0 || h <= 0) return nullptr;
	if (!cap_tex || cap_tex->w != (uint32_t)w || cap_tex->h != (uint32_t)h) {
		destroy_texture(cap_tex);
		std::vector<uint32_t> blank(w * h, 0xff000000u);
		cap_tex = create_texture(w, h, blank.data());
		cap_tex->opaque = true;
	}
	uint32_t *dst = (uint32_t *)cap_tex->mem;
	for (int j = 0; j < h; j++)
		for (int i = 0; i < w; i++) dst[j * w + i] = 0xff000000u | shown[(y + j) * W + x + i];
	cap_tex->linear = false;
	return cap_tex;
}

const Texture *capture_texture(const Texture *rt) {
	if (!rt) return nullptr;
	if (!cap_tex || cap_tex->w != rt->w || cap_tex->h != rt->h) {
		destroy_texture(cap_tex);
		std::vector<uint32_t> blank(rt->w * rt->h, 0xff000000u);
		cap_tex = create_texture(rt->w, rt->h, blank.data());
		cap_tex->opaque = true;
	}
	memcpy(cap_tex->mem, rt->mem, rt->w * rt->h * 4);
	cap_tex->linear = rt->linear;
	return cap_tex;
}

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
