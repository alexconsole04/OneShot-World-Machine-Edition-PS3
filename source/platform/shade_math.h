// shade_math.h - the per-pixel colour maths shared by every backend.
//
// The RSX fragment program (shaders/sprite.fcg) implements exactly this,
// and the host software renderer calls it directly, so host screenshots
// predict what the PS3 draws.
//
// Mirrors the game's HueShader: sample * colour, hue rotation, + tone.
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

static inline RGBA apply(RGBA tex, float cr, float cg, float cb, float ca,
                         float tone_r, float tone_g, float tone_b, float hue_rad) {
	RGBA o = { tex.r * cr, tex.g * cg, tex.b * cb, tex.a * ca };
	hue_rotate(o.r, o.g, o.b, hue_rad);
	o.r = clamp01(o.r + tone_r);
	o.g = clamp01(o.g + tone_g);
	o.b = clamp01(o.b + tone_b);
	return o;
}

} // namespace shade
