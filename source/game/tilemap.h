// tilemap.h - port of OneShotMG.src.Map: TileSet, TileLayer, TileMapManager.
#pragma once
#include "../engine/types.h"
#include "../platform/gfx.h"
#include "event_data.h"
#include "entity.h"
#include <list>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

struct GameState;
class Player;
class EventRunner;

struct TilesetInfo {   // oneshot_tilesets.json entry (TilesetInfoJson)
	int id = 0;
	std::string panorama_name;
	std::vector<uint8_t> passages, priorities, terrain_tags;
};

class TileSet {
public:
	bool Read(int firstTileId, const std::string &source);
	int GetFirstTileID() const { return firstTileId; }
	void DrawTile(Vec2 drawPos, int tileID, GameTone tone, float alpha = 1.0f) const;
	void Update();
	void ResolveTexture();
private:
	int firstTileId = 0;
	Vec2 tileSize;
	int totalTiles = 0, tileColumns = 1;
	std::string imageFileName;
	const gfx::Texture *tex = nullptr;
	bool autotile = false, animated = false;
	int animAreaRows = 0, animFrames = 0, animFrameTime = 0, animTimer = 0, frameIndex = 0;
};

class TileLayer {
public:
	explicit TileLayer(Vec2 mapSize);
	TileLayer(Vec2 mapSize, const std::string &csv, Vec2 layerSize);
	int16_t GetTileId(int x, int y) const { return tiles[x + y * layerSize.x]; }
	void SetTileId(int x, int y, int16_t id) { tiles[x + y * layerSize.x] = id; }
	int16_t GetRPGMakerTileId(int x, int y, const std::vector<TileSet> &tilesets) const;
	void DrawTile(Vec2 drawPos, int xIndex, int yIndex, const std::vector<TileSet> &tilesets, int height, GameTone tone) const;
	void Draw(const std::vector<TileSet> &tilesets, Vec2 camPos, Vec2 tileSize, bool wrapping, GameTone tone, float alpha = 1.0f) const;
private:
	std::vector<int16_t> tiles;
	Vec2 layerSize;
};

class TileMap {
public:
	TileMap(GameState *state);
	~TileMap();
	bool LoadGlobalData();                                  // tilesets.json + common events
	bool LoadMap(int mapId, float playerTileX, float playerTileY, Direction facing = Direction::Down);
	void Update();
	void Draw();

	// queries used by entities
	bool IsTileSolid(int x, int y) const;
	bool IsTileSolidId(int tileid) const;
	void SetTileSolid(int x, int y);
	uint8_t GetTilePriority(int tileid) const;
	void DrawTile(int tileId, Vec2 drawPos, float alpha, GameTone tone) const;
	void TileCollision(Rect tileCollision, Entity *entity, int tileX = 0, int tileY = 0, bool isMapTile = false);
	void KeepEntityOnMap(Entity *e);
	int GetNextEntityID() { return ++entityIDcounter; }
	void AddEntity(Entity *e);           // takes ownership
	Player *GetPlayer() const { return player; }
	GameState *State() const { return state; }
	Entity *GetEntityByID(int id) const;
	void RemoveEntity(Entity *e);
	void EntityAddEntity(Entity *e) { entitiesToSpawn.push_back(e); }   // takes ownership

	// ---- events (TileMapManager event half)
	bool CheckIfStartScript(Entity *e, bool investigateButtonPressed, Entity **interactable);
	void StartEvent(Entity *triggeringEntity, bool parallelProcess = false);
	void StartCommonEvent(int commonEventId, bool parallelProcess = false);
	const CommonEvent *GetCommonEvent(int id) const;
	Entity *GetNPCTriggeringEvent() const;
	void EventRunnerUpdate();
	void DrawEventRunners();
	void DrawMapTransition();
	bool IsInScript() const { return mainEventRunner != nullptr; }
	bool IsMapTransitioning() const { return changeMapTimer > 0; }
	bool IsChangingMap() const { return changeMapTimer < totalChangeMapTime; }
	void ChangeMap(int mapId, float xTile, float yTile, float time, Direction direction);
	void ExecuteMapTransition(const std::string &transitionName);
	void ScrollMap(int direction, int distance, int speed);
	bool IsMapScrolling() const { return scrollDistanceRemaining > 0; }
	void SetScreenTone(GameTone tone, int shiftTime);
	void SetAmbientTone(GameTone tone) { ambientTone = tone; }
	void ScreenShake(int power, int speed, int duration, bool vibrate);
	void SetPanorama(const std::string &name);
	void SetFog(const std::string &name, int hue, float opacity, gfx::Blend blend, int sx, int sy);
	bool AreAnyEntitiesInForcedRoutes() const;
	void SpawnFollowers();   // FollowerManager.SpawnAllFollowers (followers not ported yet)
	void HashedEntityCollision(Entity *e, Entity *ignoredEntity = nullptr, bool isTestCollisionEntity = false);
	int CommonEventCount() const { return (int)commonEvents.size(); }
	int ParallelRunnerCount() const { return (int)parallelEventRunners.size(); }
	bool Wrapping = false;
	class MenuManager *menus = nullptr;   // set by main; Player checks IsMenuOpen
	int debugLayers = 0;   // 0 all, 1 no entities, 2 no height layers, 3 base layers only
	int GetMapID() const { return currentMapId; }
	Vec2 GetCamPos() const { return camPos; }
	int EntityCount() const { return (int)entities.size(); }
	// Debug helper: nearest non-solid tile that has floor drawn on it.
	Vec2 FindOpenTileNear(int x, int y) const;
	Vec2 GetMapSize() const { return mapSize; }

private:
	struct MapLine;
	void MapCollision(Entity *entity);
	void UpdateCommonEvents();
	void ReleaseRetired();
	void HashEntities();
	Rect GetEntityHashRect(Entity *e, Vec2 ePos) const;
	void RemoveEntityFromHash(Entity *e);
	void UpdateEntityHeight(Entity *e);
	void UpdateCamera();
	void DrawEntity(Entity *e, GameTone tone);
	Vec2 GetMaxCamPos() const;

	GameState *state;
	std::map<int, TilesetInfo> tilesetInfos;
	const TilesetInfo *curInfo = nullptr;
	std::vector<TileSet> tilesets;
	std::map<int, std::vector<std::unique_ptr<TileLayer>>> heightsToLayers;
	std::vector<bool> collisionTiles, counterTiles;
	std::vector<uint8_t> terrainTiles;
	std::vector<std::unique_ptr<Entity>> entities;
	std::list<Entity *> entitiesByHeight;
	std::unordered_map<int, std::vector<Entity *>> hashedEntities;
	std::unordered_map<int, Entity *> entitiesById;
	std::unique_ptr<EventFile> eventFile;
	std::vector<std::unique_ptr<MapLine>> mapLines;
	std::unordered_map<int, std::vector<MapLine *>> hashedMapLines;
	Player *player = nullptr;
	Entity *camFocusEntity = nullptr;
	Vec2 mapSize, tileSize, hashTileSize, hashMapSize;
	Vec2 camPos, oldCamPos, scrollOffset;
	int currentMapId = 0, currentTilesetId = 0, entityIDcounter = 0;
	GameTone ambientTone, screenTone;
	int bulbLightTimer = 0;
	float shakeOffset = 0;
	int shakePower = 0, shakeSpeed = 0, shakeDuration = 0, shakeDirection = 1;
	GameTone startScreenTone, targetScreenTone;
	int screenToneShiftTimer = 0, screenToneShiftTotalTime = 0;
	Direction scrollDirection = Direction::None;
	int scrollSpeed = 0, scrollDistanceRemaining = 0;
	int changeMapTimer = 0, totalChangeMapTime = 0;
	bool wasMapChangedDuringUpdate = false;
	std::map<int, CommonEvent> commonEvents;
	std::unique_ptr<EventRunner> mainEventRunner;
	// C# Dictionary<int, EventRunner>: iterated in insertion order.
	std::vector<std::pair<int, std::unique_ptr<EventRunner>>> parallelEventRunners;
	int queuedCommonEventId = -1;
	std::vector<Entity *> entitiesToKill, entitiesToSpawn;
	// Entities removed from the map (erased events, previous map) stay alive
	// while an event runner may still point at them (C#'s GC did this).
	std::vector<std::unique_ptr<Entity>> retired;
};
