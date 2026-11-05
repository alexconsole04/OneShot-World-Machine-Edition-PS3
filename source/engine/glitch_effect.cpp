// glitch_effect.cpp - see glitch_effect.h.
#include "glitch_effect.h"
#include "util.h"
#include <algorithm>

static const int CYCLE_TIME = 5;   // new strips every 5 frames while glitching

GlitchEffect::GlitchEffect(Vec2 size, int strength, int timeBetween, int variance, int glitchTime)
	: size(size), strength(std::min(strength, size.x)), timeBetween(timeBetween), variance(variance),
	  glitchTime(glitchTime), nextTimer(util::random(0, timeBetween + variance)) {}

void GlitchEffect::Update() {
	if (overallTimer > 0) {
		overallTimer--;
		if (++cycleTimer >= CYCLE_TIME) {
			cycleTimer = 0;
			Generate();
		}
		return;
	}
	if (--nextTimer <= 0) {
		nextTimer = util::random(timeBetween, timeBetween + variance);
		overallTimer = glitchTime;
		cycleTimer = 0;
		Generate();
	}
}

void GlitchEffect::Generate() {   // generateGlitchSegments
	strips.clear();
	if (size.x <= 0) return;
	for (int left = size.y; left > 0;) {
		Strip s;
		s.shift = util::random(-strength, strength);   // generateSegmentWidth: wrapped into 0..width-1
		while (s.shift < 0) s.shift += size.x;
		while (s.shift >= size.x) s.shift -= size.x;
		s.height = std::min(util::random(size.y / 4, size.y / 2), left);
		if (s.height <= 0) s.height = left;   // (a picture under 4 pixels tall)
		float c[3] = { 1.0f, util::frandom(0.75f, 1.0f), 1.0f };   // generateGlitchColor
		util::apply_hue(c, util::frandom(0, 6.2831853f));
		s.r = (uint8_t)(c[0] * 255) / 255.0f;
		s.g = (uint8_t)(c[1] * 255) / 255.0f;
		s.b = (uint8_t)(c[2] * 255) / 255.0f;
		strips.push_back(s);
		left -= s.height;
	}
}

void GlitchEffect::Draw(const gfx::Texture *t, Vec2 pos, float alpha, float red, float green, float blue,
                        gfx::Blend blend, int scale) const {
	if (!t) return;
	if (overallTimer <= 0 || strips.empty()) {
		gm::blit(t, pos, Rect(0, 0, (int)t->w, (int)t->h), alpha, 0, blend, scale, GameTone(), red, green, blue);
		return;
	}
	// Each strip: its right part (from `shift`) first, then the left part
	// after it, so the strip is rotated sideways by `shift` pixels.
	Vec2 p = pos;
	int y = 0;
	for (const Strip &s : strips) {
		float r = s.r * red, g = s.g * green, b = s.b * blue;
		gm::blit(t, p, Rect(s.shift, y, size.x - s.shift, s.height), alpha, 0, blend, scale, GameTone(), r, g, b);
		gm::blit(t, Vec2(p.x + size.x - s.shift, p.y), Rect(0, y, s.shift, s.height), alpha, 0, blend, scale, GameTone(), r,
		         g, b);
		y += s.height;
		p.y += s.height;
	}
}
