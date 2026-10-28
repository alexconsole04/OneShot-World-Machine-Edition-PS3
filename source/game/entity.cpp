// entity.cpp - see entity.h. Follows Entity.cs.
#include "entity.h"
#include "tilemap.h"
#include "game_state.h"
#include "../engine/gm.h"
#include "../platform/sys.h"
#include "player.h"
#include "../engine/util.h"
#include "../engine/audio.h"

#include <algorithm>
#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>

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
	return list && list->size() > 1;
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
	for (auto &p : eventData->pages)
		if (p.trigger == 3 && p.condition.variable_valid && p.condition.variable_id == 4) { HandlesMapEventVar = true; break; }
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
			for (auto &mc : p.move_route->list)
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
		prelockDirection = Direction::None;
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
	moveRoute = page.move_route;
	moveRouteForcing = false;
	moveRouteIndex = 0;
	directionFix = page.direction_fix;
	moveAnimation = page.walk_anime;
	stopAnimation = page.step_anime;
	blendMode = page.graphic.blend_type == 1 ? gfx::Blend::Additive : gfx::Blend::Normal;
	eventTrigger = (EventTrigger)page.trigger;
	list = page.list;
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
	if (isTouchingPlayerAndTriggeredEvent) {
		Entity *pl = map->GetPlayer();
		if (StartScriptOnPlayerTouch()) {
			Rect pc(pl->pos.x - 1, pl->pos.y - 1, 2, 2);
			Rect bp = pl->GetButtonPressRect();
			isTouchingPlayerAndTriggeredEvent = !IgnoreNpcCollision() ? TouchingRect(bp) : TouchingRect(pc);
		} else if (StartScriptOnEventTouch()) {
			Rect r(pl->pos.x + pl->collisionRect.x * 256, pl->pos.y + pl->collisionRect.y * 256,
			       pl->collisionRect.w * 256, pl->collisionRect.h * 256);
			isTouchingPlayerAndTriggeredEvent = TouchingRect(r);
		} else {
			isTouchingPlayerAndTriggeredEvent = false;
		}
	}
	CommonUpdate();
}

void Entity::CommonUpdate() {
	if (KillEntityAfterUpdate) return;
	CheckForActivePage();
	if (!active) return;
	if (eventTrigger == EventTrigger::AutoRun) map->StartEvent(this);
	else if (eventTrigger == EventTrigger::ParallelProcess) map->StartEvent(this, true);
	if (jumping) { UpdateJump(); Movement(); }
	else if (moving) UpdateMovement();
	else { vel = Vec2(); frameTimer++; stopTimer++; Movement(); }
	int num = 18 - moveSpeed * 2;
	if (!stopAnimation) num = num * 2 / 3;
	if (frameTimer > num) {
		frameTimer = 0;
		if (!stopAnimation && stopTimer > 0) frameIndex = originalFrameIndex;
		else {
			frameIndex = (frameIndex + 1) % 4;
			if (IsPlayer()) CheckIfPlayFootstepSound();
		}
	}
	if (waitTimer > 0) waitTimer--;
	else if (moveRouteForcing) MoveTypeCustom();
	else if (!locked && stopTimer > (40 - moveFrequency * 2) * (6 - moveFrequency)) DoNextMove();
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

// ---------------------------------------------------------------- move routes
Rect Entity::GetButtonPressRect() const {
	Rect r;
	switch (direction) {
	case Direction::Left: r = Rect(pos.x + (collisionRect.x - 1) * 256, pos.y - 1, 256, 2); break;
	case Direction::Right: r = Rect(pos.x + (collisionRect.x + collisionRect.w) * 256, pos.y - 1, 256, 2); break;
	case Direction::Up: r = Rect(pos.x - 1, pos.y + (collisionRect.y - 1) * 256, 2, 256); break;
	default: r = Rect(pos.x - 1, pos.y + (collisionRect.y + collisionRect.h) * 256, 2, 256); break;
	}
	return r;
}

void Entity::ForceMoveRoute(const MoveRoutePtr &route) {
	if (!storedMoveRoute) {
		storedMoveRoute = moveRoute;
		storedMoveRouteIndex = moveRouteIndex;
	}
	moveRoute = route;
	moveRouteIndex = 0;
	moveRouteForcing = true;
	prelockDirection = Direction::None;
	waitTimer = 0;
	if (moveStuckTimer > 0) CancelMoveDueToMoveStuck();
	MoveTypeCustom();
}

void Entity::Lock() {
	if (locked) return;
	prelockDirection = direction;
	TurnTowardPlayer();
	locked = true;
}

void Entity::Unlock() {
	if (!locked) return;
	locked = false;
	if (!directionFix && prelockDirection != Direction::None) {
		direction = prelockDirection;
		prelockDirection = Direction::None;
	}
}

void Entity::DoNextMove() {
	switch (moveType) {
	case MoveType::Approach: {
		Vec2 pp = map->GetPlayer()->GetPos();
		if (iabs(pp.x - pos.x) + iabs(pp.y - pos.y) > 81920) { MoveRandom(); break; }
		switch (util::random(0, 5)) {
		case 0: case 1: case 2: case 3: MoveTowardPlayer(); break;
		case 4: MoveRandom(); break;
		case 5: MoveInDirection(direction); break;
		}
		break;
	}
	case MoveType::Random:
		switch (util::random(0, 5)) {
		case 0: case 1: case 2: case 3: MoveRandom(); break;
		case 4: MoveInDirection(direction); break;
		case 5: stopTimer = 0; break;
		}
		break;
	case MoveType::Custom: MoveTypeCustom(); break;
	case MoveType::Fixed: break;
	}
}

enum MoveCode {
	MC_End, MC_MoveDown, MC_MoveLeft, MC_MoveRight, MC_MoveUp, MC_MoveLowerLeft, MC_MoveLowerRight,
	MC_MoveUpperLeft, MC_MoveUpperRight, MC_MoveRandom, MC_MoveTowardPlayer, MC_MoveAwayFromPlayer,
	MC_MoveForward, MC_MoveBackward, MC_MoveJump, MC_Wait, MC_TurnDown, MC_TurnLeft, MC_TurnRight, MC_TurnUp,
	MC_TurnRight90, MC_TurnLeft90, MC_Turn180, MC_TurnRightOrLeft90, MC_TurnRandom, MC_TurnTowardPlayer,
	MC_TurnAwayFromPlayer, MC_FlagOn, MC_FlagOff, MC_ChangeSpeed, MC_ChangeFreq, MC_MoveAnimOn, MC_MoveAnimOff,
	MC_StopAnimOn, MC_StopAnimOff, MC_DirectionFixOn, MC_DirectionFixOff, MC_ThroughOn, MC_ThroughOff,
	MC_AlwaysOnTopOn, MC_AlwaysOnTopOff, MC_ChangeGraphic, MC_ChangeOpacity, MC_ChangeBlending, MC_PlaySE, MC_Script
};

static int ip(const MoveCommand &mc, size_t i) { return i < mc.parameters.size() ? atoi(mc.parameters[i].c_str()) : 0; }

void Entity::MoveTypeCustom() {
	if (moving || jumping || !moveRoute) return;
	// Hold a reference: SetNPCSheet etc. can't drop it, but End swaps routes.
	MoveRoutePtr route = moveRoute;
	while (moveRouteIndex < (int)route->list.size()) {
		const MoveCommand &mc = route->list[moveRouteIndex];
		int code = mc.code;
		if (code == MC_End) {
			if (route->repeat) { moveRouteIndex = 0; break; }
			if (moveRouteForcing) {
				moveRouteForcing = false;
				moveRoute = storedMoveRoute;
				moveRouteIndex = storedMoveRouteIndex;
				storedMoveRoute.reset();
			}
			stopTimer = 0;
			break;
		}
		if (code <= MC_MoveJump) {
			switch (code) {
			case MC_MoveDown: MoveInDirection(Direction::Down); break;
			case MC_MoveLeft: MoveInDirection(Direction::Left); break;
			case MC_MoveRight: MoveInDirection(Direction::Right); break;
			case MC_MoveUp: MoveInDirection(Direction::Up); break;
			case MC_MoveLowerLeft: MoveDiagonal(Direction::Down, Direction::Left); break;
			case MC_MoveLowerRight: MoveDiagonal(Direction::Down, Direction::Right); break;
			case MC_MoveUpperLeft: MoveDiagonal(Direction::Up, Direction::Left); break;
			case MC_MoveUpperRight: MoveDiagonal(Direction::Up, Direction::Right); break;
			case MC_MoveJump: MoveJump(ip(mc, 0), ip(mc, 1)); break;
			case MC_MoveRandom: MoveRandom(); break;
			case MC_MoveTowardPlayer: MoveTowardPlayer(); break;
			case MC_MoveAwayFromPlayer: MoveAwayFromPlayer(); break;
			case MC_MoveBackward: MoveBackward(); break;
			case MC_MoveForward: MoveInDirection(direction); break;
			}
			if (route->skippable || moving || jumping) moveRouteIndex++;
			break;
		}
		if (code == MC_Wait) {
			waitTimer = ip(mc, 0) * 2 - 1;
			moveRouteIndex++;
			break;
		}
		if (code <= MC_TurnAwayFromPlayer) {
			switch (code) {
			case MC_TurnDown: Turn(Direction::Down); break;
			case MC_TurnLeft: Turn(Direction::Left); break;
			case MC_TurnRight: Turn(Direction::Right); break;
			case MC_TurnUp: Turn(Direction::Up); break;
			case MC_TurnRight90: TurnRight90(); break;
			case MC_TurnLeft90: TurnLeft90(); break;
			case MC_Turn180: Turn180(); break;
			case MC_TurnRightOrLeft90: TurnRightOrLeft90(); break;
			case MC_TurnRandom: TurnRandom(); break;
			case MC_TurnTowardPlayer: TurnTowardPlayer(); break;
			case MC_TurnAwayFromPlayer: TurnAwayFromPlayer(); break;
			}
			moveRouteIndex++;
			break;
		}
		switch (code) {
		case MC_FlagOn: state->set_flag(ip(mc, 0), true); break;
		case MC_FlagOff: state->set_flag(ip(mc, 0), false); break;
		case MC_ChangeSpeed: moveSpeed = ip(mc, 0); break;
		case MC_ChangeFreq: moveFrequency = ip(mc, 0); break;
		case MC_MoveAnimOn: moveAnimation = true; break;
		case MC_MoveAnimOff:
			moveAnimation = false;
			if (!stopAnimation && frameIndex != originalFrameIndex) frameIndex = originalFrameIndex;
			break;
		case MC_StopAnimOn: stopAnimation = true; break;
		case MC_StopAnimOff:
			stopAnimation = false;
			if (!moveAnimation && frameIndex != originalFrameIndex) frameIndex = originalFrameIndex;
			break;
		case MC_DirectionFixOn: directionFix = true; break;
		case MC_DirectionFixOff: directionFix = false; break;
		case MC_ThroughOn: ignoreMapCollision = true; ignoreNpcCollision = true; break;
		case MC_ThroughOff: ignoreMapCollision = spawnedOnMapCollision; ignoreNpcCollision = false; break;
		case MC_AlwaysOnTopOn: alwaysOnTop = true; break;
		case MC_AlwaysOnTopOff: alwaysOnTop = false; break;
		case MC_ChangeGraphic: {
			tileId = 0;
			SetNPCSheet(mc.parameters.empty() ? std::string() : mc.parameters[0], ip(mc, 1));
			direction = (Direction)ip(mc, 2);
			int f = ip(mc, 3);
			if (originalFrameIndex != f) { frameIndex = f; originalFrameIndex = f; }
			break;
		}
		case MC_ChangeOpacity: opacity = ip(mc, 0); break;
		case MC_ChangeBlending: blendMode = ip(mc, 0) == 1 ? gfx::Blend::Additive : gfx::Blend::Normal; break;
		case MC_PlaySE:
			audio::play_sound(mc.audio_file.name.c_str(), mc.audio_file.volume / 100.0f, mc.audio_file.pitch / 100.0f);
			break;
		case MC_Script: {
			const std::string s = mc.parameters.empty() ? std::string() : mc.parameters[0];
			if (s == "straighten") { Straighten(); moveRouteIndex++; return; }
			if (s == "step_fwd_no_snap") { StepForwardNoSnap(); moveRouteIndex++; return; }
			if (s == "move_to_this_npc") { MoveToNpc(); moveRouteIndex++; return; }
			break;
		}
		default: sys::log("move command %d not implemented\n", code); break;
		}
		moveRouteIndex++;
	}
}

void Entity::MoveJump(int dx, int dy) {
	if (dx != 0 || dy != 0) {
		if (iabs(dx) > iabs(dy)) Turn(dx < 0 ? Direction::Left : Direction::Right);
		else Turn(dy < 0 ? Direction::Up : Direction::Down);
	}
	Vec2 t = GetCurrentTile();
	Vec2 t2(t.x + dx, t.y + dy);
	if ((dx == 0 && dy == 0) || Passable(t2, Direction::None)) {
		moveTarget = Vec2(t2.x * 16 * 256 + 2048, t2.y * 16 * 256 + 2048);
		int dist = (int)sqrt((double)(dx * dx + dy * dy));
		jumpPeak = 10 + dist - moveSpeed;
		jumpCount = jumpPeak * 2;
		stopTimer = 0;
		jumping = true;
		ignoreMapCollisionBeforeJump = ignoreMapCollision;
		ignoreNpcCollisionBeforeJump = ignoreNpcCollision;
		ignoreMapCollision = true;
		ignoreNpcCollision = true;
	}
}

Vec2 Entity::TargetTileFromDirection(Vec2 t, Direction d) {
	switch (d) {
	case Direction::Down: t.y++; break;
	case Direction::Up: t.y--; break;
	case Direction::Left: t.x--; break;
	case Direction::Right: t.x++; break;
	default: break;
	}
	return t;
}

bool Entity::Passable(Vec2 cur, Direction d) {
	if (ignoreMapCollision && ignoreNpcCollision) return true;
	Vec2 v = TargetTileFromDirection(cur, d);
	if (!ignoreMapCollision && map->IsTileSolid(v.x, v.y)) return false;
	if (!ignoreNpcCollision) {
		Entity test(map, state);
		test.collisionRect = Rect(-8, -8, 16, 16);
		test.pos = Vec2(v.x * 256 * 16 + 2048, v.y * 256 * 16 + 2048);
		test.ResetCollisionDirection();
		map->HashedEntityCollision(&test, this, true);
		if (test.collisionDirection != 0) return false;
	}
	return true;
}

void Entity::Turn(Direction d) {
	if (!directionFix) {
		stopTimer = 0;
		direction = d;
	}
}

void Entity::TurnRight90() {
	switch (direction) {
	case Direction::Left: Turn(Direction::Up); break;
	case Direction::Up: Turn(Direction::Right); break;
	case Direction::Right: Turn(Direction::Down); break;
	case Direction::Down: Turn(Direction::Left); break;
	default: break;
	}
}

void Entity::TurnLeft90() {
	switch (direction) {
	case Direction::Left: Turn(Direction::Down); break;
	case Direction::Up: Turn(Direction::Left); break;
	case Direction::Right: Turn(Direction::Up); break;
	case Direction::Down: Turn(Direction::Right); break;
	default: break;
	}
}

void Entity::Turn180() {
	switch (direction) {
	case Direction::Left: Turn(Direction::Right); break;
	case Direction::Up: Turn(Direction::Down); break;
	case Direction::Right: Turn(Direction::Left); break;
	case Direction::Down: Turn(Direction::Up); break;
	default: break;
	}
}

void Entity::TurnRightOrLeft90() {
	if (util::random(0, 1) == 0) TurnLeft90();
	else TurnRight90();
}

void Entity::TurnRandom() {
	switch (util::random(0, 3)) {
	case 0: Turn(Direction::Left); break;
	case 1: Turn(Direction::Right); break;
	case 2: Turn(Direction::Up); break;
	case 3: Turn(Direction::Down); break;
	}
}

void Entity::TurnTowardPlayer() {
	Vec2 pp = map->GetPlayer()->GetPos();
	int dx = pos.x - pp.x, dy = pos.y - pp.y;
	if (iabs(dx) > iabs(dy)) Turn(dx > 0 ? Direction::Left : Direction::Right);
	else Turn(dy > 0 ? Direction::Up : Direction::Down);
}

void Entity::TurnAwayFromPlayer() {
	Vec2 pp = map->GetPlayer()->GetPos();
	int dx = pos.x - pp.x, dy = pos.y - pp.y;
	if (iabs(dx) > iabs(dy)) Turn(dx > 0 ? Direction::Right : Direction::Left);
	else Turn(dy > 0 ? Direction::Down : Direction::Up);
}

void Entity::MoveToNpc() {
	Vec2 t = GetCurrentTile();
	Entity *npc = map->GetNPCTriggeringEvent();
	if (!npc) return;
	Vec2 t2 = npc->GetCurrentTile();
	if (t == t2) return;
	int dx = t.x - t2.x, dy = t.y - t2.y;
	if (iabs(dx) > iabs(dy)) {
		if (dx > 0) Turn(Direction::Left);
		else if (dx < 0) Turn(Direction::Right);
	} else if (dy > 0) Turn(Direction::Up);
	else if (dy < 0) Turn(Direction::Down);
	moveTarget = Vec2(t2.x * 16 * 256 + 2048, t2.y * 16 * 256 + 2048);
	moving = true;
	stopTimer = 0;
}

void Entity::MoveTowardPlayer() {
	Vec2 t = GetCurrentTile(), t2 = map->GetPlayer()->GetCurrentTile();
	if (t == t2) return;
	int dx = t.x - t2.x, dy = t.y - t2.y;
	if (iabs(dx) > iabs(dy)) {
		if (dx > 0) MoveInDirection(Direction::Left);
		else if (dx < 0) MoveInDirection(Direction::Right);
		if (!moving) {
			if (dy > 0) MoveInDirection(Direction::Up);
			else if (dy < 0) MoveInDirection(Direction::Down);
		}
		return;
	}
	if (dy > 0) MoveInDirection(Direction::Up);
	else if (dy < 0) MoveInDirection(Direction::Down);
	if (!moving) {
		if (dx > 0) MoveInDirection(Direction::Left);
		else if (dx < 0) MoveInDirection(Direction::Right);
	}
}

void Entity::MoveAwayFromPlayer() {
	Vec2 pp = map->GetPlayer()->GetPos();
	int dx = pos.x - pp.x, dy = pos.y - pp.y;
	if (iabs(dx) > iabs(dy)) {
		MoveInDirection(dx > 0 ? Direction::Right : Direction::Left);
		if (!moving) MoveInDirection(dy > 0 ? Direction::Down : Direction::Up);
		return;
	}
	MoveInDirection(dy > 0 ? Direction::Down : Direction::Up);
	if (!moving) MoveInDirection(dx > 0 ? Direction::Right : Direction::Left);
}

void Entity::MoveBackward() {
	switch (direction) {
	case Direction::Left: MoveInDirection(Direction::Right); Turn(Direction::Left); break;
	case Direction::Right: MoveInDirection(Direction::Left); Turn(Direction::Right); break;
	case Direction::Up: MoveInDirection(Direction::Down); Turn(Direction::Up); break;
	case Direction::Down: MoveInDirection(Direction::Up); Turn(Direction::Down); break;
	default: break;
	}
}

void Entity::MoveInDirection(Direction d, bool turnEnabled) {
	if (turnEnabled) Turn(d);
	Vec2 t = GetCurrentTile();
	if (Passable(t, d)) {
		Turn(d);
		Vec2 v = TargetTileFromDirection(t, d);
		moveTarget = Vec2(v.x * 16 * 256 + 2048, v.y * 16 * 256 + 2048);
		moving = true;
		stopTimer = 0;
		EmitFootprint();
	}
}

void Entity::Straighten() {
	Vec2 t = GetCurrentTile();
	moveTarget = Vec2(t.x * 16 * 256 + 2048, t.y * 16 * 256 + 2048);
	moving = true;
	stopTimer = 0;
}

void Entity::StepForwardNoSnap() {
	moveTarget = pos;
	switch (direction) {
	case Direction::Down: moveTarget.y += 4096; break;
	case Direction::Up: moveTarget.y -= 4096; break;
	case Direction::Left: moveTarget.x -= 4096; break;
	case Direction::Right: moveTarget.x += 4096; break;
	default: break;
	}
	moving = true;
	stopTimer = 0;
	EmitFootprint();
}

// MoveLowerLeft / LowerRight / UpperLeft / UpperRight share one shape.
void Entity::MoveDiagonal(Direction vert, Direction horiz) {
	if (!directionFix) {
		Direction oppV = vert == Direction::Down ? Direction::Up : Direction::Down;
		Direction oppH = horiz == Direction::Left ? Direction::Right : Direction::Left;
		if (direction == oppV) direction = vert;
		else if (direction == oppH) direction = horiz;
	}
	Vec2 t = GetCurrentTile();
	Vec2 th = TargetTileFromDirection(t, horiz), tv = TargetTileFromDirection(t, vert);
	if ((Passable(t, horiz) && Passable(th, vert)) || (Passable(t, vert) && Passable(tv, horiz))) {
		Vec2 v = TargetTileFromDirection(th, vert);
		moveTarget = Vec2(v.x * 16 * 256 + 2048, v.y * 16 * 256 + 2048);
		moving = true;
		stopTimer = 0;
	}
}

void Entity::MoveRandom() {
	switch (util::random(0, 3)) {
	case 0: MoveInDirection(Direction::Up, false); break;
	case 1: MoveInDirection(Direction::Left, false); break;
	case 2: MoveInDirection(Direction::Right, false); break;
	case 3: MoveInDirection(Direction::Down, false); break;
	}
}

void Entity::CheckIfPlayFootstepSound() {
	if (frameIndex % 2 != 1) return;
	float vol;
	const std::vector<std::string> *ss = map->GetStepSounds(Vec2(pos.x / 256, pos.y / 256), &vol);
	if (ss && !ss->empty()) {
		std::string name = (*ss)[util::random(0, (int)ss->size() - 1)];
		float v = (float)util::random(70, 90) * vol / 100.0f;
		float pitch = (float)util::random(85, 115) / 100.0f;
		if (state->flag(112)) {
			name = "wheel_squeak1";
			pitch = (float)util::random(120, 130) / 100.0f;
			if (wheelSqueak) pitch += 0.1f;
			wheelSqueak = !wheelSqueak;
		}
		audio::play_sound(name.c_str(), v, pitch);
	}
	EmitFootsplash();
}

// ---------------------------------------------------------------- footprints
void Entity::EmitFootprint() {
	Vec2 t = GetCurrentTile();
	if (map->IsTileCounter(t.x, t.y) && !state->flag(101) && !state->flag(111))
		map->EntityAddEntity(new Footprint(map, state, direction, pos, false));
}

void Entity::EmitFootsplash() {
	Vec2 t = GetCurrentTile();
	if (map->IsTileCounter(t.x, t.y) && state->flag(101) && !state->flag(111))
		map->EntityAddEntity(new Footprint(map, state, direction, pos, true));
}

Footprint::Footprint(TileMap *m, GameState *s, Direction d, Vec2 p, bool sp) : Entity(m, s), splash(sp) {
	eventData = &ownData;
	id = -1;
	direction = d;
	opacity = 255;
	alwaysOnBottom = true;
	NeverHash = true;
	collisionRect = Rect(0, 0, 0, 0);
	ignoreMapCollision = ignoreNpcCollision = true;
	pos = p;
	if (!splash) {
		oldPos = pos;
		drawRect = Rect(0, 0, 16, 16);
		switch (d) {
		case Direction::Left: drawRect.y = 16; break;
		case Direction::Right: drawRect.y = 32; break;
		case Direction::Up: drawRect.y = 64; break;
		default: break;
		}
	} else {
		frameIndex = 0;
		oldPos = pos;
		pos.y += 1536;
		switch (d) {
		case Direction::Down: pos.y -= 1024; break;
		case Direction::Left: pos.x += 1024; break;
		case Direction::Right: pos.x -= 1024; break;
		case Direction::Up: pos.y += 1024; break;
		default: break;
		}
	}
}

void Footprint::Update() {
	opacity -= splash ? 6 : 4;
	if (splash) frameIndex = 20 - opacity / 13;
	if (opacity <= 0) {
		opacity = 0;
		KillEntityAfterUpdate = true;
	}
}

void Footprint::Draw(Vec2 camPos, GameTone tone) {
	float a = opacity / 255.0f;
	if (!splash) {
		const gfx::Texture *t = gm::texture("footprints/footprints");
		if (!t) return;
		// The PC asks for row 64 of a 64-px-tall sheet when walking up; its
		// sampler wraps, so that is row 0 again. Wrap the same way here (the
		// RSX clamps, which stretched the last pixel row into a long streak).
		Rect src = drawRect;
		if (t->h > 0) src.y %= (int)t->h;
		gm::blit(t, Vec2(pos.x / 256 - camPos.x - 8, pos.y / 256 - camPos.y - 8), src, a, 0, gfx::Blend::Normal, 2, tone);
	} else {
		const gfx::Texture *t = gm::texture("footprints/foot_splash");
		Rect src(frameIndex % 4 * 80, frameIndex / 4 * 80, 80, 80);
		gm::blit(t, Vec2(pos.x / 256 - camPos.x - 40, pos.y / 256 - camPos.y - 40), src, a, 0, gfx::Blend::Normal, 2, tone);
	}
}

// ---------------------------------------------------------------- mood bubbles
Moodbubble::Moodbubble(TileMap *m, GameState *s, int t, Entity *p) : Entity(m, s), type(t), parent(p) {
	eventData = &ownData;
	id = -1;
	alwaysOnTop = true;
	frameTimer = 0;
	NeverHash = true;
	parentOffsetY = -(parent->GetSpriteSize().y / 4 + 24);
	pos = Vec2(parent->GetPos().x, parent->GetPos().y + parentOffsetY * 256);
	oldPos = pos;
	collisionRect = Rect(0, 0, 0, 0);
	ignoreMapCollision = ignoreNpcCollision = true;
	drawRect = Rect(0, 0, 16, 16);
	switch (type) {
	case 1: drawRect.y = 0; break;    // Question
	case 2: drawRect.y = 16; break;   // Exclamation
	case 3: drawRect.y = 80; break;   // Love
	case 4: drawRect.y = 96; break;   // Sweat
	case 5: drawRect.y = 32; break;   // Frustration
	default: break;
	}
}

void Moodbubble::Update() {
	oldPos = pos;
	frameTimer++;
	switch (frameTimer) {
	case 2: parentOffsetY -= 3; break;
	case 4: parentOffsetY -= 2; break;
	case 6: parentOffsetY--; break;
	case 10: parentOffsetY++; break;
	case 12: parentOffsetY += 2; break;
	case 14: parentOffsetY += 3; break;
	default: break;
	}
	pos = Vec2(parent->GetPos().x, parent->GetPos().y + parentOffsetY * 256);
	if (type == 5) drawRect.y = 32 + frameTimer / 4 % 3 * 16;
	if (frameTimer > 40) KillEntityAfterUpdate = true;
}

void Moodbubble::Draw(Vec2 camPos, GameTone tone) {
	const gfx::Texture *t = gm::texture("npc/mood_bubbles");
	if (!t) return;
	gm::blit(t, Vec2(pos.x / 256 - camPos.x - 8, pos.y / 256 - camPos.y - 8), drawRect, 1.0f, 0, gfx::Blend::Normal, 2, tone);
}

// ---------------------------------------------------------------- save data
#include "cJSON.h"

static cJSON *vec_json(Vec2 v) {
	cJSON *o = cJSON_CreateObject();
	cJSON_AddNumberToObject(o, "X", v.x);
	cJSON_AddNumberToObject(o, "Y", v.y);
	return o;
}

static Vec2 json_vec(const cJSON *o, const char *k, Vec2 def) {
	const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
	if (!cJSON_IsObject(v)) return def;
	const cJSON *x = cJSON_GetObjectItemCaseSensitive(v, "X"), *y = cJSON_GetObjectItemCaseSensitive(v, "Y");
	return Vec2(cJSON_IsNumber(x) ? (int)x->valuedouble : def.x, cJSON_IsNumber(y) ? (int)y->valuedouble : def.y);
}

static int json_int(const cJSON *o, const char *k, int def) {
	const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
	return cJSON_IsNumber(v) ? (int)v->valuedouble : def;
}

static bool json_bool(const cJSON *o, const char *k, bool def) {
	const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
	return cJSON_IsBool(v) ? cJSON_IsTrue(v) : def;
}

cJSON *Entity::GetSaveData() const {
	cJSON *o = cJSON_CreateObject();
	cJSON_AddNumberToObject(o, "id", id);
	cJSON_AddNumberToObject(o, "direction", (int)direction);
	cJSON_AddNumberToObject(o, "frameIndex", frameIndex);
	cJSON_AddNumberToObject(o, "frameTimer", frameTimer);
	cJSON_AddNumberToObject(o, "collisionDirection", collisionDirection);
	cJSON_AddNumberToObject(o, "currentPageIndex", currentPageIndex);
	cJSON_AddItemToObject(o, "pos", vec_json(pos));
	cJSON_AddItemToObject(o, "vel", vec_json(vel));
	cJSON_AddBoolToObject(o, "active", active);
	cJSON_AddNumberToObject(o, "stopTimer", stopTimer);
	cJSON_AddNumberToObject(o, "moveRouteIndex", moveRouteIndex);
	cJSON_AddNumberToObject(o, "waitTimer", waitTimer);
	cJSON_AddNumberToObject(o, "moveStuckTimer", moveStuckTimer);
	cJSON_AddBoolToObject(o, "jumping", jumping);
	cJSON_AddBoolToObject(o, "moving", moving);
	cJSON_AddItemToObject(o, "moveTarget", vec_json(moveTarget));
	cJSON_AddBoolToObject(o, "ignoreMapCollision", ignoreMapCollision);
	cJSON_AddBoolToObject(o, "ignoreNpcCollision", ignoreNpcCollision);
	if (moveRoute) cJSON_AddItemToObject(o, "moveRoute", route_to_json(*moveRoute));
	cJSON_AddBoolToObject(o, "moveRouteForcing", moveRouteForcing);
	if (storedMoveRoute) cJSON_AddItemToObject(o, "storedMoveRoute", route_to_json(*storedMoveRoute));
	return o;
}

void Entity::LoadSaveData(const cJSON *o) {
	SetActivePage(json_int(o, "currentPageIndex", currentPageIndex));
	direction = (Direction)json_int(o, "direction", (int)direction);
	if (!IsPlayer()) {
		frameIndex = json_int(o, "frameIndex", frameIndex);
		frameTimer = json_int(o, "frameTimer", frameTimer);
	}
	collisionDirection = json_int(o, "collisionDirection", collisionDirection);
	pos = json_vec(o, "pos", pos);
	vel = json_vec(o, "vel", vel);
	active = json_bool(o, "active", active);
	stopTimer = json_int(o, "stopTimer", stopTimer);
	moveRouteIndex = json_int(o, "moveRouteIndex", moveRouteIndex);
	waitTimer = json_int(o, "waitTimer", waitTimer);
	moveStuckTimer = json_int(o, "moveStuckTimer", moveStuckTimer);
	jumping = json_bool(o, "jumping", jumping);
	moving = json_bool(o, "moving", moving);
	moveTarget = json_vec(o, "moveTarget", moveTarget);
	ignoreMapCollision = json_bool(o, "ignoreMapCollision", ignoreMapCollision);
	ignoreNpcCollision = json_bool(o, "ignoreNpcCollision", ignoreNpcCollision);
	const cJSON *mr = cJSON_GetObjectItemCaseSensitive(o, "moveRoute");
	moveRoute = cJSON_IsObject(mr) ? route_from_json(mr) : nullptr;
	moveRouteForcing = json_bool(o, "moveRouteForcing", false);
	const cJSON *smr = cJSON_GetObjectItemCaseSensitive(o, "storedMoveRoute");
	storedMoveRoute = cJSON_IsObject(smr) ? route_from_json(smr) : nullptr;
}

void Entity::DebugResetPlayerState() {
	if (moveRouteForcing) {
		moveRoute = storedMoveRoute;
		moveRouteIndex = storedMoveRouteIndex;
		moveRouteForcing = false;
	}
	locked = false;
	active = true;
	opacity = 255;
	jumping = moving = false;
	vel = Vec2();
	moveTarget = pos;
	waitTimer = moveStuckTimer = 0;
	directionFix = false;
	ignoreMapCollision = ignoreNpcCollision = false;
	blendMode = gfx::Blend::Normal;
}
