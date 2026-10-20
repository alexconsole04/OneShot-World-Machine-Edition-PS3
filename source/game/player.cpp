// player.cpp - see player.h. Follows Player.cs (Update / NormalizeWalkRunSpeed).
// Not yet ported: the roomba (flag 79) branch, footprints and footstep
// sounds, and starting scripts by touch / action button.
#include "player.h"
#include "tilemap.h"
#include "game_state.h"
#include "../platform/input.h"
#include <math.h>
#include <stdlib.h>

Player::Player(TileMap *m, GameState *s, int pid, float xPixel, float yPixel) : Entity(m, s) {
	ownData.name = "player";
	ownData.pages.resize(1);
	ownData.pages[0].move_speed = 2;
	ownData.pages[0].walk_anime = true;
	eventData = &ownData;
	currentPageIndex = 0;
	moveSpeed = 2;
	moveAnimation = true;
	Initialize();
	id = pid;
	pos = Vec2((int)(xPixel * 256.0f), (int)(yPixel * 256.0f));
	oldPos = pos;
	ignoreMapCollision = false;
	ignoreNpcCollision = false;
	opacity = 255;
	npcSheet = "niko";
	ReloadTextures();
}

void Player::MapChanged(int eId, float xPixel, float yPixel) {
	SetID(eId);
	pos = Vec2((int)(xPixel * 256.0f), (int)(yPixel * 256.0f));
	oldPos = pos;
	if (moveRouteForcing && moving) { moving = false; moveTarget = pos; }
	HasBeenHashed = false;
}

bool Player::PlayerHasControl() const {
	return !map->IsInScript() && !map->IsMapTransitioning();   // + !menu open (menus: later)
}

void Player::NormalizeWalkRunSpeed(bool run) {
	int num = run ? 512 : 256;
	int num2 = (int)sqrt((double)(vel.x * vel.x + vel.y * vel.y));
	if (num2 > num) {
		vel.x = vel.x * num / num2;
		vel.y = vel.y * num / num2;
	}
}

void Player::Update() {
	if (moveRouteForcing) { CommonUpdate(); return; }
	if (PlayerHasControl()) {
		if (!map->IsInScript() && !state->flag(11)) {
			oldDirection = direction;
			bool run = input::held(input::BTN_RUN) ^ defaultMovementRun;
			bool lockDown = directionFix && direction == Direction::Down;
			bool onlyLR = state->flag(112) || state->flag(146);
			Vec2 moved = pos - oldPos;
			int sx = input::stick_x(), sy = input::stick_y();
			if (sx != 0 || sy != 0) {
				// C#: num = -MoveStickPos.Y / 100 (XNA stick Y is up-positive);
				// our stick_y is already down-positive, so num = sy / 100.
				float num = (float)sy / 100.0f, num2 = (float)sx / 100.0f;
				// PS3 port: a small margin before turning between the horizontal and
				// vertical facings, so stick noise near 45 degrees doesn't flip Niko's
				// sprite every frame. Movement itself is unchanged.
				const float FACE_MARGIN = 0.08f;
				bool facingH = direction == Direction::Left || direction == Direction::Right;
				if (num < 0.0f && !lockDown) {
					if (fabsf(num) > fabsf(num2) + (facingH ? FACE_MARGIN : 0.0f)) direction = Direction::Up;
					if (!onlyLR) vel.y = (int)((run ? 512.0f : 256.0f) * num);
				} else if (num > 0.0f) {
					if (fabsf(num) > fabsf(num2) + (facingH ? FACE_MARGIN : 0.0f)) direction = Direction::Down;
					if (!onlyLR) vel.y = (int)((run ? 512.0f : 256.0f) * num);
				} else {
					vel.y = 0;
				}
				if (num2 < 0.0f && !lockDown) {
					if (fabsf(num2) > fabsf(num) + (facingH ? 0.0f : FACE_MARGIN)) direction = Direction::Left;
					vel.x = (int)((run ? 512.0f : 256.0f) * num2);
				} else if (num2 > 0.0f && !lockDown) {
					if (fabsf(num2) > fabsf(num) + (facingH ? 0.0f : FACE_MARGIN)) direction = Direction::Right;
					vel.x = (int)((run ? 512.0f : 256.0f) * num2);
				} else {
					vel.x = 0;
				}
			} else {
				using namespace input;
				if (held(BTN_UP) && !held(BTN_DOWN) && !lockDown) {
					if (!isCollisionDirectionSet(Direction::Up) || vel.x == 0) direction = Direction::Up;
					if (!onlyLR) vel.y = run ? -512 : -256;
				} else if (held(BTN_DOWN) && !held(BTN_UP)) {
					if (!isCollisionDirectionSet(Direction::Down) || vel.x == 0) direction = Direction::Down;
					if (!onlyLR) vel.y = run ? 512 : 256;
				} else {
					vel.y = 0;
				}
				if (held(BTN_LEFT) && !held(BTN_RIGHT) && !lockDown) {
					if (!isCollisionDirectionSet(Direction::Left) || vel.y == 0) direction = Direction::Left;
					vel.x = run ? -512 : -256;
				} else if (held(BTN_RIGHT) && !held(BTN_LEFT) && !lockDown) {
					if (!isCollisionDirectionSet(Direction::Right) || vel.y == 0) direction = Direction::Right;
					vel.x = run ? 512 : 256;
				} else {
					vel.x = 0;
				}
			}
			NormalizeWalkRunSpeed(run);
			if (oldPos.x == pos.x && oldPos.y == pos.y) {
				frameIndex = 0;
				frameTimer = 0;
				footprintEmitTimer = 0;
			} else {
				frameTimer++;
				footprintEmitTimer++;
				int speed = (int)sqrt((double)(moved.x * moved.x + moved.y * moved.y));
				if (speed > 512) speed = 512;
				int frameTime = 10 - (int)rintf((float)speed * 4.0f / 512.0f);
				if (frameTimer >= frameTime) {
					frameIndex = (frameIndex + 1) % 4;
					frameTimer = 0;
				}
			}
		} else {
			vel = Vec2();
		}
	} else {
		oldDirection = direction;
		if (moveAnimation) frameIndex = 0;
		vel = Vec2();
	}
	Movement();
	ResetCollisionDirection();
	if (!map->Wrapping) map->KeepEntityOnMap(this);
}
