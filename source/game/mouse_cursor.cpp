#include "mouse_cursor.h"
#include "../engine/audio.h"
#include "../engine/gm.h"
#include "../platform/gfx.h"
#include "../platform/input.h"
#include <math.h>

Vec2 desktop_space() {
	Vec2 b = gm::boot_space();   // the desktop's draw size (1280x720 on a 16:9 TV)
	return Vec2(b.x / 2, b.y / 2);
}

// InputManager.ApplyStickDeadzone
static Vec2 apply_dead_zone(Vec2 stick, int dead, int upper) {
	double ang = atan2((double)stick.x, (double)stick.y);
	double mag = sqrt((double)stick.x * stick.x + (double)stick.y * stick.y) - dead;
	mag = mag * 100.0 / (upper - dead);
	if (mag < 0) mag = 0;
	if (mag > 100) mag = 100;
	return Vec2((int)(sin(ang) * mag), (int)(cos(ang) * mag));
}

void MouseCursor::Update() {
	// MouseStickPos = deadzoned right stick * speed; the cursor moves by
	// that / 100, rounded away from zero, each frame.
	Vec2 stick = apply_dead_zone(Vec2(input::rstick_x(), input::rstick_y()), deadZone, 90);
	double mx = stick.x * speed / 100.0, my = stick.y * speed / 100.0;
	mx = mx > 0 ? ceil(mx) : floor(mx);
	my = my > 0 ? ceil(my) : floor(my);
	Vec2 p(pos.x + (int)mx, pos.y + (int)my);
	Vec2 sz = desktop_space();
	if (p.x < 0) p.x = 0; else if (p.x >= sz.x) p.x = sz.x - 1;
	if (p.y < 0) p.y = 0; else if (p.y >= sz.y) p.y = sz.y - 1;
	pos = p;
	clicked = input::pressed(input::BTN_R2);   // InputMapping: MouseButton = right trigger
	if (clicked) audio::play_sound("twm_mouse_click");
	bool was = held;
	held = input::held(input::BTN_R2);
	if (!held && was) audio::play_sound("twm_mouse_unclick");
	if (forceState) state = forcedState;
	static const Vec2 offsets[7] = { Vec2(0, 0), Vec2(-3, 0), Vec2(0, 0), Vec2(-3, -4), Vec2(-3, -4), Vec2(-6, -6), Vec2(-5, -5) };
	offset = offsets[state];
	if (state == Clickable) {
		static const int waits[2] = { 20, 10 };
		if (++frameTimer >= waits[frameIndex]) { frameTimer = 0; frameIndex++; }
		if (frameIndex >= 2) frameIndex = 0;
	} else {
		frameTimer = 0;
		frameIndex = 0;
	}
	frameRect.x = 16 * (frameIndex + (int)state);
	state = Normal;
}

void MouseCursor::Draw() const {
	if (hidden) return;
	const gfx::Texture *t = gm::texture("the_world_machine/cursor", gm::TheWorldMachine);
	if (!t) return;
	// MainBlit at scale 2 in the desktop's draw space: the black outline
	// rows, then the white fill.
	gm::begin_space(gm::boot_space());
	Vec2 p(pos.x + offset.x, pos.y + offset.y);
	Rect outline = frameRect, fill = frameRect;
	outline.y += 16;
	fill.y += 32;
	gm::blit(t, p, outline, 1.0f, 0, gfx::Blend::Normal, 2, GameTone(), 0, 0, 0);
	gm::blit(t, p, fill, 1.0f, 0, gfx::Blend::Normal, 2);
	gm::end_space();
}
