// gfx.h - platform renderer interface.
//
// One immediate-mode sprite API with automatic batching. Consecutive draws
// that share a texture and blend mode go out as a single RSX draw call.
// Tone, hue and colour travel per vertex, so they never break a batch.
// (MonoGame's SpriteBatch had to restart whenever they changed.)
//
// Backends:  gfx_gcm.cpp  (PS3, RSX via libgcm)
//            gfx_soft.cpp (host, software rasteriser + PNG screenshots)
#pragma once
#include <stdint.h>

namespace gfx {

enum class Blend : uint8_t { Normal, Additive };

struct Texture {
	uint32_t w = 0, h = 0;
	uint32_t pitch = 0;      // bytes per row as stored
	uint32_t gpu_offset = 0; // RSX local offset (PS3)
	void    *mem = nullptr;  // backend storage (RSX local mem / host heap)
	uint32_t bytes = 0;
};

// Per-sprite shading parameters, mirroring GraphicsManager.MainBlit:
// colour multiply (red/green/blue/alpha), GameTone (-255..255 per channel),
// hue shift in degrees.
struct Shade {
	float r = 1, g = 1, b = 1, a = 1;
	int16_t tone_r = 0, tone_g = 0, tone_b = 0;
	int16_t hue = 0;
};

bool init();
void shutdown();
int  screen_w();
int  screen_h();

// argb: w*h pixels, 0xAARRGGBB in host order (straight alpha).
Texture *create_texture(uint32_t w, uint32_t h, const uint32_t *argb);
void     destroy_texture(Texture *t);

void begin_frame(uint32_t clear_argb);
// Draw texel rect (sx,sy,sw,sh) of t into screen rect (dx,dy,dw,dh).
void draw(const Texture *t, float dx, float dy, float dw, float dh,
          int sx, int sy, int sw, int sh, const Shade &s, Blend blend);
void end_frame();

struct Stats {
	int draw_calls;   // GPU draw calls issued last frame
	int quads;        // sprites submitted last frame
	uint32_t tex_bytes_used, tex_bytes_total;
};
Stats last_stats();

// Host only: write the last presented frame to a PNG (no-op on PS3).
bool screenshot(const char *path);

} // namespace gfx
