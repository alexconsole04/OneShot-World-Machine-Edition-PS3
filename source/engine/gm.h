// gm.h - port of the drawing half of GraphicsManager: named textures in
// cache groups, and MainBlit in the game's 640x480 "OneShot window" space,
// mapped onto the TV with an aspect-correct, centred scale.
#pragma once
#include "types.h"
#include <string>
#include <vector>
#include "../platform/gfx.h"

namespace gm {

// TextureCache.CacheType
enum CacheType { Game, TheWorldMachine, CharacterProfile, GalleryWindow, DesktopWallpaper,
                 BootScreen, Toasts, Achievements, Glyphs, CACHE_COUNT };

void init();

// Look up (loading on first use) content/<name>.xnb. Returns nullptr and
// logs once if it can't be loaded. Pointers stay valid until the cache
// group is cleared.
const gfx::Texture *texture(const char *name, CacheType cache = Game);
Vec2 texture_size(const char *name, CacheType cache = Game);
bool texture_exists(const char *name, CacheType cache = Game);
void clear_cache(CacheType cache);
// IsPixelSolid: that texel's alpha is over 128 (false outside the texture).
bool pixel_solid(const char *name, Vec2 texel, CacheType cache = Game);
// Maps a texture name to the current language's version of it (set by the
// language manager; content/<dir>/<lang>/<name>).
void set_texture_redirect(std::string (*fn)(const std::string &));
// Textures that aren't game files (the player's own wallpapers): asked
// first for every name not yet cached; returns null for names it doesn't make.
void set_texture_source(gfx::Texture *(*fn)(const std::string &name));
// Read a texture in the background ahead of use (content/<name>.xnb); the
// next texture() call for it finds it ready, or waits for that read.
void prefetch(const char *name, CacheType cache = Game);
// Each frame: turns finished background reads into textures.
void pump_prefetch();
// Forget that a name was missing (its source can make it now).
void forget_missing(const char *name);
// Start a new "generation" (a map change): textures not used during the last
// `keep` generations are released. The PC clears the whole Game cache on
// every map change; keeping the textures the previous maps used means
// walking between neighbouring maps doesn't reload and re-upload the same
// tilesets and sprites each time. Falls back to releasing everything unused
// when VRAM use is high.
void new_generation(CacheType cache, int keep = 2);
// Debug: names/textures currently in a cache group (sorted by name).
int cache_list(CacheType cache, const char **names, const gfx::Texture **texs, int max);

// MainBlit(texture, pixelPos, srcRect, alpha, hue, blend, scale, tone, r, g, b).
// pixelPos is multiplied by scale (the C# overloads do the same) and lands in
// 640x480 window space.
void blit(const gfx::Texture *t, Vec2 pixelPos, Rect src, float alpha = 1.0f, int hue = 0,
          gfx::Blend blend = gfx::Blend::Normal, int scale = 2, GameTone tone = GameTone(),
          float red = 1.0f, float green = 1.0f, float blue = 1.0f);
// Whole-texture convenience.
void blit(const gfx::Texture *t, Vec2 pixelPos, float alpha = 1.0f, int hue = 0,
          gfx::Blend blend = gfx::Blend::Normal, int scale = 2, GameTone tone = GameTone());

// MainBlit(tex, pixelPos, srcRect, xScale, yScale, ...): pixelPos already in
// 640x480 window space, destination size = src size * scale (truncated).
void blit_scaled(const gfx::Texture *t, Vec2 windowPos, Rect src, float xScale, float yScale,
                 float alpha = 1.0f, int hue = 0, gfx::Blend blend = gfx::Blend::Normal,
                 GameTone tone = GameTone(), float red = 1.0f, float green = 1.0f, float blue = 1.0f);
// FillScreen: tile src across the 320x240 game screen starting at pos
// (in units of `scale`), as GraphicsManager.FillScreen.
void fill_screen(const gfx::Texture *t, Vec2 pos, Rect src, float alpha = 1.0f, gfx::Blend blend = gfx::Blend::Normal,
                 GameTone tone = GameTone(), int hue = 0, int scale = 2);
// ColorBoxBlit(rect, colour, blend, scale): solid box, rect in game pixels.
void color_box(Rect r, uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha,
               gfx::Blend blend = gfx::Blend::Normal, int scale = 2);

// Map-transition snapshot (GraphicsManager.SetUpMapTransitionFrame /
// DrawMapTransitionFrame): grab the last presented 640x480 window, then draw
// it over the new map with fading alpha.
void capture_window();
void draw_captured_window(float alpha);

// Scaling of the 640x480 window to the TV (1080p is 2.25x, 720p 1.5x):
//  SHARP   draw at a whole-number scale (2x at both 1080p and 720p), then
//          filter to the TV size: every game pixel the same size, edges
//          stay crisp (only their outermost line is blended)
//  SMOOTH  draw at 1x and filter up: soft, like the PC's linear scaling
// (m14 dropped INTEGER and DIRECT; saved values of those read as SHARP.)
enum { SCALE_SHARP = 0, SCALE_SMOOTH = 1, SCALE_COUNT = 2 };
void set_scaling(int mode);
// Where the 640x480 game window lands on the TV (TV pixels, TV pixels per
// window pixel): the desktop moves it with the OneShot window. reset puts
// it back to the whole TV (aspect-fitted).
void set_window_placement(float ox, float oy, float scale);
void reset_window_placement();
// A point of a size.x x size.y space (begin_space) in TV pixels.
Vec2 space_to_tv(Vec2 size, float x, float y, float *scale);
const char *scaling_info();   // for the debug overlay
// Window drawing goes to an offscreen target between these two calls
// (window coordinates keep working), then end_window puts it on the TV.
void begin_window();
void end_window();
// GlitchEffectManager / DrawGlitchSegments: the next end_window draws the
// picture in horizontal bands, each shifted left by `shift` window pixels
// (wrapping round) and tinted. Bands go down from the top; nullptr: off.
struct GlitchSegment { int shift, height; uint8_t r, g, b; };
void set_window_glitch(const std::vector<GlitchSegment> *segments);

// Screen-space helpers for overlays (raw TV pixels).
void blit_screen(const gfx::Texture *t, float x, float y, float w, float h, Rect src,
                 const gfx::Shade &s, gfx::Blend blend = gfx::Blend::Normal);
const gfx::Texture *white();   // 1x1 white texture for boxes/text

// Boot screen / desktop drawing space: between begin_space and end_space,
// window coordinates (blit, color_box, fonts) are pixels of a size.x x
// size.y screen fitted to the whole TV.
Vec2 boot_space();   // GraphicsManager.GetBootScreenSize (1280x720 on a 16:9 TV)
void begin_space(Vec2 size);
void end_space();
// Blend::Dither parameters for the current space at this opacity.
void set_dither(float opacity);

// Clip drawing to the 640x480 OneShot window / back to full screen.
void clip_to_window();
void clip_off();

float window_scale();          // TV pixels per window pixel (1.5 at 720p)
Vec2 window_origin();          // TV position of the window's top-left

} // namespace gm
