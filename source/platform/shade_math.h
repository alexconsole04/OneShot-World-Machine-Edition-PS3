// shade_math.h - the per-pixel colour maths shared by every backend.
//
// The RSX fragment program (shaders/sprite.fcg) implements exactly this,
// and the host software renderer calls it directly, so host screenshots
// predict what the PS3 draws.
//
// Mirrors the game's HueShader exactly (verified against its GLSL).
#pragma once
#include <math.h>

namespace shade {

struct RGBA { float r, g, b, a; };

static inline float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

// Hue rotation about the grey axis (Rodrigues rotation in RGB space).
static inline void hue_rotate(float &r, float &g, float &b, float rad) {
	if (rad == 0.0f) return;
	const float c = cosf(rad), s = sinf(rad), k = (1.0f - c) / 3.0f, q = 0.57735026f * s;
	float nr = r * (c + k) + g * (k - q) + b * (k + q);
	float ng = r * (k + q) + g * (c + k) + b * (k - q);
	float nb = r * (k - q) + g * (k + q) + b * (c + k);
	r = nr; g = ng; b = nb;
}

// Same order as the game's HueShader (MGFX GLSL in content/shaders):
//   rgb = (hue_rotate(tex.rgb) + tone) * gamma;  out = vec4(rgb, tex.a) * vertexColour
static inline RGBA apply(RGBA tex, float cr, float cg, float cb, float ca,
                         float tone_r, float tone_g, float tone_b, float hue_rad, float gamma = 1.0f) {
	float r = tex.r, g = tex.g, b = tex.b;
	hue_rotate(r, g, b, hue_rad);
	r = (r + tone_r) * gamma;
	g = (g + tone_g) * gamma;
	b = (b + tone_b) * gamma;
	return { clamp01(r * cr), clamp01(g * cg), clamp01(b * cb), clamp01(tex.a * ca) };
}

} // namespace shade
