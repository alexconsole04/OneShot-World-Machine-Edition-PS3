// entity.h - port of OneShotMG.src.Entity (movement, pages, drawing, collision).
// Event-script execution and move routes arrive with the interpreter milestone.
#pragma once
#include "../engine/types.h"
#include "../platform/gfx.h"
#include "event_data.h"
#include <list>
#include <string>

class TileMap;
struct GameState;

class Entity {
public:
	enum class MoveType { Fixed = 0, Random = 1, Approach = 2, Custom = 3 };
	enum class EventTrigger { ActionButton = 0, PlayerTouch = 1, EventTouch = 2, AutoRun = 3, ParallelProcess = 4 };

	Entity(TileMap *map, GameState *state);
	Entity(TileMap *map, GameState *state, const EventData *data);
	virtual ~Entity() = default;

	void Initialize();
	virtual void Update();
	virtual void Movement();
	virtual void Draw(Vec2 camPos, GameTone tone);
	virtual bool IsPlayer() const { return false; }

	int GetID() const { return id; }
	void SetID(int i) { id = i; }
	bool Active() const { return active; }
	bool IgnoreMapCollision() const { return ignoreMapCollision; }
	bool IgnoreNpcCollision() const { return ignoreNpcCollision; }
	const std::string &GetName() const;
	bool HasScript() const;

	int GetPixelBottom() const;
	Vec2 GetPixelPos() const { return Vec2(pos.x / 256, pos.y / 256); }
	Vec2 GetCurrentTile() const { return Vec2(pos.x / 4096, pos.y / 4096); }
	bool HasMoved() const { return oldPos.x != pos.x || oldPos.y != pos.y; }
	Vec2 GetPos() const { return pos; }
	Vec2 GetOldPos() const { return oldPos; }
	void SetPos(Vec2 p) { pos = p; }
	void SetPosTile(Vec2 t) { pos = Vec2(t.x * 16 * 256 + 2048, t.y * 16 * 256 + 2048); oldPos = pos; moving = false; moveTarget = pos; vel = Vec2(); }
	Vec2 GetVel() const { return vel; }
	void SetVel(Vec2 v) { vel = v; }
	Rect GetCollisionRect() const { return collisionRect; }
	Direction GetDirection() const { return direction; }
	void SetDirection(Direction d) { direction = d; }

	void ResetCollisionDirection() { collisionDirection = 0; }
	static int CollisionFlagMask(Direction d) { return 1 << ((int)d / 2); }
	void SetCollisionDirection(Direction d) { collisionDirection |= CollisionFlagMask(d); }
	bool isCollisionDirectionSet(Direction d) const { return (collisionDirection & CollisionFlagMask(d)) != 0; }

	void ReloadTextures();   // re-resolve sheet/lightmap after a cache clear
	bool TouchingRect(Rect o) const;
	void EntityCollision(Entity *other);

	EventTrigger eventTrigger = EventTrigger::ActionButton;
	bool NeverHash = false;
	bool HasBeenHashed = false;
	bool KillEntityAfterUpdate = false;
	std::list<Entity *>::iterator sortedNode;

protected:
	void CheckForActivePage();
	void SetActivePage(int idx);
	bool isPageActive(const EventPage &p) const;
	void SetNPCSheet(const std::string &sheet, int hue);
	void CommonUpdate();
	void UpdateMovement();
	void UpdateJump();
	void CancelMoveDueToMoveStuck();
	void HandleSpecialCollision();
	bool MovesEver() const;

	TileMap *map;
	GameState *state;
	EventData ownData;          // for synthesized entities (player, clones)
	const EventData *eventData = nullptr;

	MoveType moveType = MoveType::Fixed;
	Direction direction = Direction::Down, originalDirection = Direction::None, oldDirection = Direction::Down;
	int frameIndex = 0, frameTimer = 0, originalFrameIndex = 0;
	int collisionDirection = 0;
	int tileId = 0;
	std::string npcSheet;
	const gfx::Texture *sheetTex = nullptr, *lightmapTex = nullptr;
	bool hasLightmap = false;
	Vec2 spriteSize;
	int hue = 0, opacity = 255;
	gfx::Blend blendMode = gfx::Blend::Normal;
	int currentPageIndex = -1;
	Vec2 oldPos, pos, vel;
	bool active = true;
	Rect collisionRect;
	bool ignoreMapCollision = true, ignoreNpcCollision = true;
	bool alwaysOnTop = false, alwaysOnBottom = false, spawnedOnMapCollision = false;
	int stopTimer = 0, moveFrequency = 0, moveSpeed = 0;
	bool moveAnimation = false, stopAnimation = false;
	bool moveRouteForcing = false, locked = false;
	int waitTimer = 0, moveStuckTimer = 0;
	bool jumping = false, moving = false;
	int jumpCount = 0, jumpPeak = 0;
	bool ignoreNpcCollisionBeforeJump = false, ignoreMapCollisionBeforeJump = false;
	Vec2 moveTarget;
	bool directionFix = false;
	int id = 0;
	bool AlwaysActiveAndOnlyHasOnePage = false;
	const EventPage *activePage = nullptr;
	std::vector<bool> pageThrough;   // per-page 'through' (C# mutates page data)
	std::vector<bool> pageBottom;    // per-page always_on_bottom overrides
};
