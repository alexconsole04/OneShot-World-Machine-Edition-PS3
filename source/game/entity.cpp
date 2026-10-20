// entity.cpp - see entity.h. Follows Entity.cs.
#include "entity.h"
#include "tilemap.h"
#include "game_state.h"
#include "../engine/gm.h"
#include "../platform/sys.h"
#include "cJSON.h"

#include <algorithm>
#include <ctype.h>
#include <stdint.h>

static std::string lower(std::string s) {
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)tolower(c); });
	return s;
}

Entity::Entity(TileMap *m, GameState *s) : map(m), state(s) {}

Entity::Entity(TileMap *m, GameState *s, const EventData *data) : map(m), state(s), eventData(data) {
	Initialize();
}

const std::string &Entity::GetName() const {
	static const std::string empty;
	return eventData ? eventData->name : empty;
}

bool Entity::HasScript() const {
	if (eventTrigger == EventTrigger::ParallelProcess || eventTrigger == EventTrigger::AutoRun) return false;
	return activePage && activePage->list && cJSON_GetArraySize(activePage->list) > 1;
}

void Entity::Initialize() {
	id = eventData->id;
	pos = Vec2(eventData->x * 16 * 256 + 2048, eventData->y * 16 * 256 + 2048);
	oldPos = pos;
	vel = Vec2();
	collisionRect = Rect(-8, -8, 16, 16);
	pageThrough.clear();
	for (auto &p : eventData->pages) pageThrough.push_back(p.through);
	if (map->IsTileSolid(eventData->x, eventData->y) && !IsPlayer()) {
		spawnedOnMapCollision = true;
		ignoreMapCollision = true;
	}
	std::string nm = lower(eventData->name);
	pageBottom.assign(eventData->pages.size(), false);
	for (size_t i = 0; i < eventData->pages.size(); i++) pageBottom[i] = eventData->pages[i].always_on_bottom;
	if (nm == "!bigpool" || nm == "!smallpool" || nm == "!generator" || nm == "!bed" || nm.compare(0, 6, "pixel ") == 0) {
		alwaysOnBottom = true;
		std::fill(pageBottom.begin(), pageBottom.end(), true);
	} else if (nm == "!minecart") {
		collisionRect = Rect(-16, -8, 32, 16);
	} else {
		alwaysOnBottom = false;
	}
	HandleSpecialCollision();
	if (eventData->pages.size() == 1) {
		const auto &c = eventData->pages[0].condition;
		if (!c.self_switch_valid && !c.switch1_valid && !c.switch2_valid && !c.variable_valid) {
			AlwaysActiveAndOnlyHasOnePage = true;
			active = true;
			SetActivePage(0);
		}
	}
	if (AlwaysActiveAndOnlyHasOnePage) {
		std::string cn = lower(eventData->pages[0].graphic.character_name);
		if (cn == "sparkle_blue1" || cn == "sparkle_blue2" || cn == "water_waves_blue" ||
		    cn == "water_waves_blue2" || cn == "water_waves_blue_wide") {
			if (!ignoreNpcCollision) {
				Vec2 t = GetCurrentTile();
				map->SetTileSolid(t.x, t.y);
			}
			ignoreMapCollision = true;
			ignoreNpcCollision = true;
			std::fill(pageThrough.begin(), pageThrough.end(), true);
		}
		if (!HasScript() && !IsPlayer() && !MovesEver()) {
			NeverHash = true;
			if (!ignoreNpcCollision) {
				Vec2 t = GetCurrentTile();
				map->SetTileSolid(t.x, t.y);
				ignoreNpcCollision = true;
				ignoreMapCollision = true;
			}
		}
	} else {
		CheckForActivePage();
	}
}

bool Entity::MovesEver() const {
	for (auto &p : eventData->pages) {
		MoveType mt = (MoveType)p.move_type;
		if (mt == MoveType::Random || mt == MoveType::Approach) return true;
		if (mt == MoveType::Custom)
			for (auto &mc : p.move_route.list)
				if ((unsigned)(mc.code - 1) <= 13u) return true;
	}
	return false;
}

void Entity::HandleSpecialCollision() {
	static const float bed[][2] = { { 0, 0 }, { 0, -1 } };
	static const float specialpool[][2] = { { -1, 0 }, { 0, 0 }, { 1, 0 }, { -1, -1 }, { 0, -1 }, { 1, -1 } };
	static const float vendor[][2] = { { -1, 0 }, { 0, 0 }, { 1, 0 } };
	static const int bigpool[][2] = { { -4, -7 }, { -3, -7 }, { -2, -7 }, { -1, -7 }, { 0, -7 }, { 1, -7 }, { 2, -7 }, { 3, -7 },
		{ 4, -7 }, { -5, -6 }, { -5, -5 }, { -5, -4 }, { 5, -6 }, { 5, -5 }, { 5, -4 }, { -4, -3 }, { -3, -3 }, { 3, -3 },
		{ 4, -3 }, { -2, -2 }, { -1, -2 }, { 0, -2 }, { 1, -2 }, { 2, -2 } };
	static const int smallpool[][2] = { { -1, 0 }, { 0, 0 }, { 1, 0 }, { -1, -1 }, { 0, -1 }, { 1, -1 } };

	std::string nm = lower(eventData->name);
	const float (*clones)[2] = nullptr;
	int nclones = 0;
	const int (*solids)[2] = nullptr;
	int nsolids = 0;
	bool cloneSet = false;
	if (nm == "!bigpool") { solids = bigpool; nsolids = 24; cloneSet = true; }
	else if (nm == "!smallpool") { solids = smallpool; nsolids = 6; cloneSet = true; pos.y += 1024; }
	else if (nm == "!specialpool") { clones = specialpool; nclones = 6; }
	else if (nm == "!vendor") { clones = vendor; nclones = 3; }
	else if (nm == "!glitch" || nm == "!bed" || nm == "!lens") { clones = bed; nclones = 2; }

	if (clones || cloneSet) {   // C#: array != null (bigpool/smallpool use an empty one)
		for (int i = 0; i < nclones; i++) {
			// Clones need EventData that outlives them; keep it in ownData.
			Entity *c = new Entity(map, state);
			c->ownData.id = map->GetNextEntityID();
			c->ownData.name = eventData->name + "_clone";
			c->ownData.x = eventData->x;
			c->ownData.y = eventData->y;
			for (auto &src : eventData->pages) {
				EventPage p;
				p.always_on_top = src.always_on_top;
				p.condition = src.condition;
				p.direction_fix = src.direction_fix;
				p.list = src.list;
				p.move_frequency = src.move_frequency;
				p.move_route = src.move_route;
				p.move_speed = src.move_speed;
				p.move_type = 0;
				p.step_anime = src.step_anime;
				p.through = src.through;
				p.trigger = src.trigger;
				p.walk_anime = src.walk_anime;
				p.graphic.direction = 0;
				p.graphic.opacity = 255;
				c->ownData.pages.push_back(p);
			}
			c->eventData = &c->ownData;
			c->Initialize();
			c->pos.x = (int)((eventData->x + clones[i][0] + 0.5f) * 256.0f * 16.0f);
			c->pos.y = (int)((eventData->y + clones[i][1] + 0.5f) * 256.0f * 16.0f);
			c->oldPos = c->pos;
			map->AddEntity(c);
		}
		ignoreMapCollision = true;
		ignoreNpcCollision = true;
		std::fill(pageThrough.begin(), pageThrough.end(), true);
	}
	if (solids) {
		Vec2 t = GetCurrentTile();
		for (int i = 0; i < nsolids; i++) map->SetTileSolid(t.x + solids[i][0], t.y + solids[i][1]);
	}
}

bool Entity::isPageActive(const EventPage &page) const {
	const auto &c = page.condition;
	if ((!c.switch1_valid || state->flag(c.switch1_id)) && (!c.switch2_valid || state->flag(c.switch2_id)) &&
	    (!c.variable_valid || state->var(c.variable_id) >= c.variable_value)) {
		if (c.self_switch_valid) return state->self_switch(id, c.self_switch_ch);
		return true;
	}
	return false;
}

void Entity::CheckForActivePage() {
	if (AlwaysActiveAndOnlyHasOnePage) return;
	bool found = false;
	for (int i = (int)eventData->pages.size() - 1; i >= 0; i--) {
		if (isPageActive(eventData->pages[i])) {
			SetActivePage(i);
			found = true;
			break;
		}
	}
	active = found;
}

void Entity::SetActivePage(int idx) {
	if (currentPageIndex == idx) return;
	currentPageIndex = idx;
	if (idx < 0 || idx >= (int)eventData->pages.size()) return;
	const EventPage &page = eventData->pages[idx];
	activePage = &page;
	alwaysOnTop = page.always_on_top;
	alwaysOnBottom = page.always_on_bottom || pageBottom[idx];
	tileId = page.graphic.tile_id;
	SetNPCSheet(page.graphic.character_name, page.graphic.character_hue);
	if (tileId > 384) pageThrough[idx] = pageThrough[idx] || !map->IsTileSolidId(tileId);
	if (originalDirection != (Direction)page.graphic.direction || page.direction_fix) {
		direction = (Direction)page.graphic.direction;
		originalDirection = direction;
	}
	if (page.graphic.pattern != originalFrameIndex) {
		frameIndex = page.graphic.pattern;
		originalFrameIndex = frameIndex;
	}
	opacity = page.graphic.opacity;
	ignoreNpcCollision = pageThrough[idx];
	ignoreMapCollision = pageThrough[idx] || spawnedOnMapCollision;
	moveType = (MoveType)page.move_type;
	moveSpeed = page.move_speed;
	moveFrequency = page.move_frequency;
	moveRouteForcing = false;
	directionFix = page.direction_fix;
	moveAnimation = page.walk_anime;
	stopAnimation = page.step_anime;
	blendMode = page.graphic.blend_type == 1 ? gfx::Blend::Additive : gfx::Blend::Normal;
	eventTrigger = (EventTrigger)page.trigger;
}

void Entity::SetNPCSheet(const std::string &sheet, int h) {
	npcSheet = lower(sheet);
	if (state->flag(160) && npcSheet.compare(0, 4, "niko") == 0) npcSheet.replace(0, 4, "en");
	hue = h;
	ReloadTextures();
}

void Entity::ReloadTextures() {
	sheetTex = lightmapTex = nullptr;
	hasLightmap = false;
	if (!npcSheet.empty()) {
		sheetTex = gm::texture(("npc/" + npcSheet).c_str());
		spriteSize = sheetTex ? Vec2((int)sheetTex->w / 4, (int)sheetTex->h / 4) : Vec2();
		std::string lm = "lightmaps/" + npcSheet;
		hasLightmap = sys::file_exists(("content/" + lm + ".xnb").c_str());
		if (hasLightmap) lightmapTex = gm::texture(lm.c_str());
	} else if (tileId > 384) {
		spriteSize = Vec2(16, 16);
	} else {
		spriteSize = Vec2();
	}
	std::string nm = eventData ? lower(eventData->name) : std::string();
	if (npcSheet == "green_scenery1" && nm == "maize") alwaysOnBottom = true;
}

int Entity::GetPixelBottom() const {
	if (tileId > 384) {
		uint8_t pr = map->GetTilePriority(tileId);
		if (pr == 0) return INT32_MIN;
		return pos.y / 256 + collisionRect.y + collisionRect.h + (pr - 1) * 16;
	}
	if (alwaysOnBottom) return INT32_MIN;
	if (alwaysOnTop) return INT32_MAX;
	return pos.y / 256 + collisionRect.y + collisionRect.h;
}

void Entity::Movement() {
	oldPos = pos;
	pos.x += vel.x;
	pos.y += vel.y;
}

void Entity::Update() {
	CommonUpdate();
}

void Entity::CommonUpdate() {
	if (KillEntityAfterUpdate) return;
	CheckForActivePage();
	if (!active) return;
	// AutoRun / ParallelProcess events start here once the interpreter lands.
	if (jumping) { UpdateJump(); Movement(); }
	else if (moving) UpdateMovement();
	else { vel = Vec2(); frameTimer++; stopTimer++; Movement(); }
	int num = 18 - moveSpeed * 2;
	if (!stopAnimation) num = num * 2 / 3;
	if (frameTimer > num) {
		frameTimer = 0;
		if (!stopAnimation && stopTimer > 0) frameIndex = originalFrameIndex;
		else frameIndex = (frameIndex + 1) % 4;
	}
	if (waitTimer > 0) waitTimer--;
	// Move routes / random movement: next milestone (DoNextMove, MoveTypeCustom).
	ResetCollisionDirection();
}

void Entity::UpdateMovement() {
	if (moveAnimation || stopAnimation) frameTimer++;
	int num = 16 * (2 << moveSpeed);
	if (oldPos.x == pos.x && oldPos.y == pos.y) { moveStuckTimer++; stopTimer = 1; }
	else { moveStuckTimer = 0; stopTimer = 0; }
	if (pos.x + num <= moveTarget.x) vel.x = num;
	else if (pos.x - num >= moveTarget.x) vel.x = -num;
	else { vel.x = 0; pos.x = moveTarget.x; }
	if (pos.y + num <= moveTarget.y) vel.y = num;
	else if (pos.y - num >= moveTarget.y) vel.y = -num;
	else { vel.y = 0; pos.y = moveTarget.y; }
	Movement();
	if ((pos.x == moveTarget.x && pos.y == moveTarget.y) || (moveStuckTimer > 30 && !moveRouteForcing && moveType != MoveType::Custom))
		CancelMoveDueToMoveStuck();
}

void Entity::CancelMoveDueToMoveStuck() {
	moveStuckTimer = 0;
	moving = false;
	vel = Vec2();
	stopTimer = 0;
}

void Entity::UpdateJump() {
	jumpCount--;
	if (jumpCount <= 0) {
		jumpPeak = 0;
		jumping = false;
		pos = moveTarget;
		ignoreMapCollision = ignoreMapCollisionBeforeJump;
		ignoreNpcCollision = ignoreNpcCollisionBeforeJump;
	} else {
		pos.x = (pos.x * jumpCount + moveTarget.x) / (jumpCount + 1);
		pos.y = (pos.y * jumpCount + moveTarget.y) / (jumpCount + 1);
	}
}

bool Entity::TouchingRect(Rect o) const {
	Rect r(pos.x + collisionRect.x * 256, pos.y + collisionRect.y * 256, collisionRect.w * 256, collisionRect.h * 256);
	return r.x <= o.x + o.w && r.x + r.w >= o.x && r.y <= o.y + o.h && r.y + r.h >= o.y;
}

void Entity::EntityCollision(Entity *o) {
	Rect r(o->pos.x + o->collisionRect.x * 256, o->pos.y + o->collisionRect.y * 256,
	       o->collisionRect.w * 256, o->collisionRect.h * 256);
	if (TouchingRect(r)) map->TileCollision(r, this);
}

void Entity::Draw(Vec2 camPos, GameTone tone) {
	if (!active) return;
	Vec2 z(pos.x / 256 - camPos.x, pos.y / 256 - camPos.y);
	z.x -= spriteSize.x / 2;
	z.y -= spriteSize.y - 8;
	if (jumping) {
		int n = jumpCount - jumpPeak;
		z.y -= (jumpPeak * jumpPeak - n * n) / 4;
	}
	if (z.x > 320 || z.x + spriteSize.x < 0 || z.y > 240 || z.y + spriteSize.y < 0) return;
	float a = opacity / 255.0f;
	if (!npcSheet.empty()) {
		int row = ((int)direction - 2) / 2;
		Rect src(frameIndex * spriteSize.x, row * spriteSize.y, spriteSize.x, spriteSize.y);
		gm::blit(sheetTex, z, src, a, hue, blendMode, 2, tone);
		if (hasLightmap && lightmapTex) {
			Vec2 sz(spriteSize.x * 2, spriteSize.y * 2);
			gm::blit(lightmapTex, Vec2(z.x * 2, z.y * 2), Rect(frameIndex * sz.x, row * sz.y, sz.x, sz.y),
			         a, hue, gfx::Blend::Additive, 1);
		}
	} else if (tileId > 384) {
		map->DrawTile(tileId - 384, z, a, tone);
	}
}
