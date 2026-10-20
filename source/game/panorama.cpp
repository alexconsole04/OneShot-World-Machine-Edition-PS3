#include "panorama.h"
#include "../engine/gm.h"
#include "../engine/util.h"

Panorama::Panorama(const std::string &name, Vec2 sz) : source(name), size(sz) {
	std::string base = util::lower(name.substr(name.find('/') + 1));
	if (base == "red" || base == "red_distort") type = Type::Clamped;
	else if (base == "codebg") type = Type::Scroll;
	else if (base == "green_water" || base == "dark_water") {
		type = Type::AnimatedWater;
		back = source + "3";
		front = source + "1";
	} else if (base == "skyline") {
		offset_y = 32;
	}
}

void Panorama::Update() {
	if (type == Type::AnimatedWater) {
		frontAlpha += 3;
		if (frontAlpha > 255) {
			frontAlpha %= 255;
			frameIndex = (frameIndex + 1) % 3;
			back = front;
			front = source + std::to_string(frameIndex + 1);
		}
	} else if (type == Type::Scroll && size.x > 0 && size.y > 0) {
		scrollOffset.x = (scrollOffset.x + 1) % (size.x * 4);
		scrollOffset.y = (scrollOffset.y + 1) % (size.y * 4);
	}
}

void Panorama::Draw(Vec2 cam, Vec2 player, Vec2 mapSize, Vec2 tileSize, GameTone tone) const {
	if (size.x <= 0 || size.y <= 0) return;
	Rect src(0, 0, size.x, size.y);
	Vec2 z;
	switch (type) {
	case Type::Scroll:
		z.x = -scrollOffset.x / 4 - cam.x;
		z.y = -scrollOffset.y / 4 - cam.y;
		z.x %= size.x;
		z.y %= size.y;
		gm::fill_screen(gm::texture(source.c_str()), z, src, 1.0f, gfx::Blend::Normal, tone);
		break;
	case Type::Clamped:
		z.x = -(player.x * (size.x - 320)) / (mapSize.x * tileSize.x);
		z.y = -(player.y * (size.y - 240)) / (mapSize.y * tileSize.y);
		if (z.x > 0) z.x -= size.x;
		if (z.y > 0) z.y -= size.y;
		gm::fill_screen(gm::texture(source.c_str()), z, src, 1.0f, gfx::Blend::Normal, tone);
		break;
	case Type::Normal:
		z.x = -cam.x / 2;
		z.y = -cam.y / 2 + offset_y;
		z.x %= size.x;
		z.y %= size.y;
		if (z.x > 0) z.x -= size.x;
		if (z.y > 0) z.y -= size.y;
		gm::fill_screen(gm::texture(source.c_str()), z, src, 1.0f, gfx::Blend::Normal, tone);
		break;
	case Type::AnimatedWater:
		z.x = -cam.x % size.x;
		z.y = -cam.y % size.y;
		if (z.x > 0) z.x -= size.x;
		if (z.y > 0) z.y -= size.y;
		gm::fill_screen(gm::texture(back.c_str()), z, src, 1.0f, gfx::Blend::Normal, tone);
		gm::fill_screen(gm::texture(front.c_str()), z, src, frontAlpha / 255.0f, gfx::Blend::Normal, tone);
		break;
	}
}

Fog::Fog(const std::string &name, Vec2 sz, int h, float op, gfx::Blend b, int sx, int sy)
	: source(name), size(sz), hue(h), opacity(op), blend(b), scrollX(sx / 8.0f), scrollY(sy / 8.0f) {}

void Fog::Update() {
	offsetX += scrollX;
	offsetY += scrollY;
	if (offsetX < 0) offsetX += size.x;
	else if (offsetX > size.x) offsetX -= size.x;
	if (offsetY < 0) offsetY += size.y;
	else if (offsetY > size.y) offsetY -= size.y;
}

void Fog::Draw(Vec2 cam, GameTone tone) const {
	if (size.x <= 0 || size.y <= 0) return;
	Vec2 z(-cam.x * 2 + (int)offsetX, -cam.y * 2 + (int)offsetY);
	z.x %= size.x;
	z.y %= size.y;
	if (z.x > 0) z.x -= size.x;
	if (z.y > 0) z.y -= size.y;
	gm::fill_screen(gm::texture(source.c_str()), z, Rect(0, 0, size.x, size.y), opacity, blend, tone, hue, 1);
}
