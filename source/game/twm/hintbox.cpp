// hintbox.cpp - see hintbox.h. Port of HintBox.
#include "hintbox.h"
#include <math.h>

namespace twm {

HintBox::HintBox(Vec2 center, const std::string &text) : screenCenter(center) {
	int w = 412;
	lines = wrap(text, 400);
	if (lines.empty()) lines.push_back("");
	if (lines.size() <= 1) w = font::width(font::OS, lines[0], true) + 12;
	boxSize = Vec2(w, 12 + 32 * (int)lines.size());
}

Vec2 HintBox::TargetQuadrant(Vec2 t) {
	Vec2 d = t - screenCenter;
	Vec2 r = lastQuadrant;
	if (r.x == 0) r.x = d.x < 0 ? 1 : -1;
	if (r.y == 0) r.y = d.y < 0 ? 1 : -1;
	if (FadeOut) return r;
	if (d.x > 30) r.x = -1;
	else if (d.x < -30) r.x = 1;
	if (d.y > 30) r.y = -1;
	else if (d.y < -30) r.y = 1;
	lastQuadrant = r;
	return r;
}

bool HintBox::Update() {
	if (!GetTargetPos) return false;
	bool done = false;
	if (FadeOut && alpha > 0) {
		alpha = alpha > 5 ? alpha - 5 : 0;
		done = alpha == 0;
	} else if (alpha < 255) {
		alpha = alpha + 5 > 255 ? 255 : alpha + 5;
	}
	if (!FadeOut) {
		Vec2 v = GetTargetPos();
		if (v.x != 0 || v.y != 0) targetPos = v;
	}
	Vec2 q = TargetQuadrant(targetPos);
	Vec2 o(25 * q.x, 25 * q.y);
	if (o.x < 0) o.x -= boxSize.x;
	if (o.y < 0) o.y -= boxSize.y;
	o.y += (int)(sin(driftTimer * 3.14159265358979 / 180.0 * 2.0) * 6.0);   // the drift
	Vec2 half = screen();
	if (o.x + targetPos.x < 20) o.x = 20 - targetPos.x;
	else if (o.x + targetPos.x > half.x - 20 - boxSize.x) o.x = half.x - 20 - boxSize.x - targetPos.x;
	float dx = o.x - offX, dy = o.y - offY;
	float k = sqrtf(dx * dx + dy * dy) > 0 ? 0.12f : 0.02f;
	offX += dx * k;
	offY += dy * k;
	if (++driftTimer > 360) driftTimer = 0;
	return done;
}

void HintBox::Draw(const Theme &th) const {
	if (!GetTargetPos) return;
	Vec2 p = targetPos + Vec2((int)offX, (int)offY);
	Rect r(p.x, p.y, boxSize.x, boxSize.y);
	box(r, th.Primary(alpha));
	box(shrink(r, 2), th.Background(alpha));
	Vec2 t(r.x + 6, r.y + 12);
	for (auto &l : lines) {
		line(t + Vec2(-2, 4), l, th.Primary(alpha));
		text(t, l, th.Primary(alpha), font::OS, font::Mode::OnlyGlyphs);
		t.y += 32;
	}
}

} // namespace twm
