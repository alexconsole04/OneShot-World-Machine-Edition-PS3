// special_entities.cpp - see special_entities.h.
#include "special_entities.h"
#include "../core/loc.h"
#include "../core/settings.h"
#include "tilemap.h"
#include "../twm/wm.h"
#include "../../engine/font.h"
#include "../../engine/gm.h"
#include "../../engine/util.h"

// ===================================================================== Collectible

Collectible::Collectible(TileMap *m, GameState *s, const EventData *d) : Entity(m, s, d) {
	// The third page's switch says which theme it unlocks (461-469).
	static const char *themes[] = { "blue", "teal", "green", "yellow", "red", "pink", "orange", "white", "rainbow" };
	if (d->pages.size() >= 3 && d->pages[2].condition.switch1_valid) {
		int sw = d->pages[2].condition.switch1_id;
		if (sw >= 461 && sw <= 469) themeId = themes[sw - 461];
	}
}

// MathHelper.HandleAbberateUpdate
static void aberrate(int &timer, Vec2 &off) {
	if (timer > 0) {
		if (--timer <= 0) { timer = 0; off = Vec2(); }
	} else if (util::random(1, 100) < 33) {
		timer = util::random(2, 8);
		off = Vec2(util::random(-1, 1), util::random(-1, 1));
	}
}

void Collectible::Update() {
	CommonUpdate();
	if (!active) return;
	aberrate(aberrTimer[0], aberr[0]);
	aberrate(aberrTimer[2], aberr[2]);
	aberrate(aberrTimer[1], aberr[1]);
	hovering = false;
	if (map->IsInScript()) return;
	Vec2 cam = map->GetCamPos();
	Rect r(pos.x / 256 - cam.x - 12, pos.y / 256 - cam.y - 10, 24, 20);
	if (r.x + r.w <= 0 || r.x > 320 || r.y + r.h <= 0 || r.y > 240) return;
	Vec2 m;
	if (twm::game_cursor(&m) && m.x >= r.x && m.x <= r.x + r.w && m.y >= r.y && m.y <= r.y + r.h) hovering = true;
	if (hovering && twm::cursor_clicked()) {
		SetActivePage(0);
		map->StartEvent(this);
	}
}

void Collectible::Draw(Vec2 camPos, GameTone tone) {
	if (!active || npcSheet.empty() || !sheetTex) return;
	uint8_t pr[3], bg[3];
	twm::theme_rgb(themeId, pr, bg);
	float a = opacity / 255.0f;
	Vec2 z(pos.x / 256 - camPos.x, pos.y / 256 - camPos.y);
	if (jumping) {
		int n = jumpCount - jumpPeak;
		z.y -= (jumpPeak * jumpPeak - n * n) / 4;
	}
	Rect src(frameIndex * spriteSize.x, ((int)direction - 2) / 2 * spriteSize.y, spriteSize.x, spriteSize.y);
	z.x -= spriteSize.x / 2;
	z.y -= spriteSize.y - 8;
	if (hovering) {
		Rect box(pos.x / 256 - camPos.x - 12, pos.y / 256 - camPos.y - 10, 24, 20);
		gm::color_box(box, pr[0], pr[1], pr[2], (uint8_t)(opacity / 2));
		gm::color_box(Rect(box.x + 1, box.y + 1, box.w - 2, box.h - 2), bg[0], bg[1], bg[2], (uint8_t)(opacity / 2));
	}
	// A black shadow, the colour fringes, then the artifact in the theme's colour.
	gm::blit(sheetTex, Vec2(z.x + 1, z.y + 1), src, a, hue, blendMode, 2, tone, 0, 0, 0);
	float r = pr[0] / 255.0f, g = pr[1] / 255.0f, b = pr[2] / 255.0f;
	if (settings().chromaAberration) {
		gm::blit(sheetTex, z + aberr[0], src, a / 2, hue, gfx::Blend::Additive, 2, tone, r, 0, 0);
		gm::blit(sheetTex, z + aberr[1], src, a / 2, hue, gfx::Blend::Additive, 2, tone, 0, g, 0);
		gm::blit(sheetTex, z + aberr[2], src, a / 2, hue, gfx::Blend::Additive, 2, tone, 0, 0, b);
	}
	gm::blit(sheetTex, z, src, a, hue, blendMode, 2, tone, r, g, b);
}

// ===================================================================== CreditsText

CreditsText::CreditsText(TileMap *m, GameState *s, const EventData *d, bool sm) : Entity(m, s, d), small(sm) {
	alwaysOnBottom = true;
	std::fill(pageBottom.begin(), pageBottom.end(), true);
	CheckForActivePage();
	if (!d->pages.empty() && active && currentPageIndex >= 0 && currentPageIndex < (int)d->pages.size()) {
		const CommandList &l = *d->pages[currentPageIndex].list;
		if (!l.empty() && l[0].code == 101) text = l[0].param(0);
	}
	bool blank = true;
	for (char c : text)
		if (c != ' ' && c != '\t' && c != '\n' && c != '\r') blank = false;
	if (blank) KillEntityAfterUpdate = true;
	NeverHash = true;
}

void CreditsText::Update() {
	if (!KillEntityAfterUpdate) Entity::Update();
}

void CreditsText::Draw(Vec2 camPos, GameTone) {
	if (KillEntityAfterUpdate || !active || text.empty()) return;
	Vec2 z(pos.x / 256 - camPos.x, pos.y / 256 - camPos.y - 8);
	if (z.y < -16 || z.y > 256) return;
	// Both kinds are drawn at 1x in window pixels (the "@text" scale), as
	// the PC's CreditsTextEntity is created with drawScale 1.
	z = Vec2(z.x * 2, z.y * 2 + 12 + (small ? 10 : 0));
	font::draw_centered(small ? font::GameSmall : font::Game, z, loc::map_text(0, text), 0x512181, 1.0f, 1);
}
