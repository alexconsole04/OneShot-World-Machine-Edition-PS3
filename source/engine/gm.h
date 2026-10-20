// gm.h - port of the drawing half of GraphicsManager: named textures in
// cache groups, and MainBlit in the game's 640x480 "OneShot window" space,
// mapped onto the TV with an aspect-correct, centred scale.
#pragma once
#include "types.h"
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

// MainBlit(texture, pixelPos, srcRect, alpha, hue, blend, scale, tone, r, g, b).
// pixelPos is multiplied by scale (the C# overloads do the same) and lands in
// 640x480 window space.
void blit(const gfx::Texture *t, Vec2 pixelPos, Rect src, float alpha = 1.0f, int hue = 0,
          gfx::Blend blend = gfx::Blend::Normal, int scale = 2, GameTone tone = GameTone(),
          float red = 1.0f, float green = 1.0f, float blue = 1.0f);
// Whole-texture convenience.
void blit(const gfx::Texture *t, Vec2 pixelPos, float alpha = 1.0f, int hue = 0,
          gfx::Blend blend = gfx::Blend::Normal, int scale = 2, GameTone tone = GameTone());

// Screen-space helpers for overlays (raw TV pixels).
void blit_screen(const gfx::Texture *t, float x, float y, float w, float h, Rect src,
                 const gfx::Shade &s, gfx::Blend blend = gfx::Blend::Normal);
const gfx::Texture *white();   // 1x1 white texture for boxes/text

// Clip drawing to the 640x480 OneShot window / back to full screen.
void clip_to_window();
void clip_off();

float window_scale();          // TV pixels per window pixel (1.5 at 720p)
Vec2 window_origin();          // TV position of the window's top-left

} // namespace gm
