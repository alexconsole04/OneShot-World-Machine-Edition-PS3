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
	bool opaque = false;     // sample alpha as 1 (screen captures)
	bool linear = false;     // bilinear filtering (scaling the game window); else nearest
	void *aux = nullptr;     // backend: a render target's depth buffer
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

// Clip subsequent draws to a screen rect (hardware scissor on RSX).
// clip_off() restores the full screen. Mirrors the PC version drawing the
// game into a 640x480 window texture that clips at its edges.
void set_clip(int x, int y, int w, int h);
void clip_off();

struct Stats {
	int draw_calls;   // GPU draw calls issued last frame
	int quads;        // sprites submitted last frame
	uint32_t tex_bytes_used, tex_bytes_total;
};
Stats last_stats();

// Debug: RSX submission method (0 indexed triangles, 1 quads, 2 quads <=256 verts/draw).
void set_draw_mode(int mode);
int draw_mode();
const char *draw_mode_name();
// Debug: minimal fragment program (texture * colour) instead of the HueShader port.
void set_plain_shader(bool on);
bool plain_shader();
// Debug: wait for the RSX to finish every frame (no CPU/GPU overlap).
void set_full_sync(bool on);
bool full_sync();
// Debug: texture coordinates normalized (false) or in texels (true).
void set_texel_coords(bool on);
bool texel_coords();

// Offscreen render target (opaque, usable as a texture once drawn).
// nullptr if there's no memory for it. Free with destroy_texture.
Texture *create_render_target(uint32_t w, uint32_t h);
// Draw into rt from now on (nullptr = the screen). Resets the clip rect.
// end_frame() switches back to the screen.
void set_render_target(Texture *rt);
// Copy a render target's current contents into the capture texture (the
// same one capture_screen uses). Alpha reads as 1.
const Texture *capture_texture(const Texture *rt);

// Copy a rectangle of the last presented frame into an internal texture
// (reused between calls; valid until the next capture). Alpha reads as 1.
const Texture *capture_screen(int x, int y, int w, int h);

// Debug: vertex buffers in main (IO) memory (true, default) or RSX local memory.
void set_vertex_main_memory(bool on);
bool vertex_main_memory();

// Host only: write the last presented frame to a PNG (no-op on PS3).
bool screenshot(const char *path);

} // namespace gfx
