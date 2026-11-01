// tilemap.cpp - TileMapManager (see tilemap.h): the entities on the map, each
// frame's update, the camera, screen effects, drawing in height order, and
// saving. Loading, collision and events are in tilemap_load.cpp,
// tilemap_collision.cpp and tilemap_events.cpp; tilesets and layers in
// tile_layers.cpp. Method bodies follow the decompiled C# closely so
// collision and draw order behave identically.
#include "tilemap_internal.h"
#include "event_runner.h"
#include "player.h"
#include "../../engine/gm.h"
#include "../../platform/sys.h"
#include "../core/game_state.h"
#include "cJSON.h"
#include <algorithm>
#include <math.h>

// ======================================================================= TileMap
TileMap::TileMap(GameState *s) : state(s) {}
TileMap::~TileMap() = default;   // out of line: EventRunner is incomplete in the header

void TileMap::AddEntity(Entity *e) {
	entities.emplace_back(e);
	if (e->GetID() >= 0) {
		if (entitiesById.count(e->GetID())) {
			sys::log("Tried to add entity with ID %d, but entity with that ID already exists!\n", e->GetID());
			return;
		}
		entitiesById[e->GetID()] = e;
	}
	if (entitiesByHeight.empty()) {
		e->sortedNode = entitiesByHeight.insert(entitiesByHeight.end(), e);
		return;
	}
	int pb = e->GetPixelBottom();
	auto it = entitiesByHeight.begin();
	while ((*it)->GetPixelBottom() < pb) {
		++it;
		if (it == entitiesByHeight.end()) {
			e->sortedNode = entitiesByHeight.insert(entitiesByHeight.end(), e);
			return;
		}
	}
	e->sortedNode = entitiesByHeight.insert(std::next(it), e);   // AddAfter
}

void TileMap::UpdateEntityHeight(Entity *e) {
	int pb = e->GetPixelBottom();
	// walk backwards
	auto node = e->sortedNode;
	auto prev = node;
	bool atFront = (node == entitiesByHeight.begin());
	auto origPrev = atFront ? entitiesByHeight.end() : std::prev(node);
	auto p = origPrev;
	while (p != entitiesByHeight.end() && (*p)->GetPixelBottom() > pb)
		p = (p == entitiesByHeight.begin()) ? entitiesByHeight.end() : std::prev(p);
	if (p != origPrev) {
		entitiesByHeight.erase(node);
		if (p == entitiesByHeight.end()) e->sortedNode = entitiesByHeight.insert(entitiesByHeight.begin(), e);
		else e->sortedNode = entitiesByHeight.insert(std::next(p), e);
	}
	(void)prev;
	// walk forwards
	node = e->sortedNode;
	auto origNext = std::next(node);
	auto n = origNext;
	while (n != entitiesByHeight.end() &&
	       ((*n)->GetPixelBottom() < pb || (e->IsPlayer() && (*n)->GetPixelBottom() <= pb)))
		++n;
	if (n != origNext) {
		entitiesByHeight.erase(node);
		e->sortedNode = entitiesByHeight.insert(n, e);   // AddBefore(next) / AddLast
	}
}

bool TileMap::IsTileSolid(int x, int y) const {
	if (x < 0 || x >= mapSize.x || y < 0 || y >= mapSize.y) return false;
	return collisionTiles[x + y * mapSize.x];
}
bool TileMap::IsTileCounter(int x, int y) const {
	if (x < 0 || x >= mapSize.x || y < 0 || y >= mapSize.y || counterTiles.empty()) return false;
	return counterTiles[x + y * mapSize.x];
}
bool TileMap::IsTileSolidId(int tileid) const { return (at(curInfo->passages, tileid) & 0xF) == 15; }
void TileMap::SetTileSolid(int x, int y) {
	if (x >= 0 && x < mapSize.x && y >= 0 && y < mapSize.y) collisionTiles[x + y * mapSize.x] = true;
}
uint8_t TileMap::GetTilePriority(int tileid) const { return at(curInfo->priorities, tileid); }

void TileMap::DrawTile(int tileId, Vec2 drawPos, float alpha, GameTone tone) const {
	const TileSet &ts = tilesets.back();
	ts.DrawTile(drawPos, tileId + ts.GetFirstTileID(), tone, alpha);
}

void TileMap::Update() {
	wasMapChangedDuringUpdate = false;
	if (hasFireflies) UpdateFireflies(Vec2(camPos.x - oldCamPos.x, camPos.y - oldCamPos.y));
	if (shakeDuration > 0 || shakeOffset != 0.0f) {
		float step = (float)(shakePower * shakeSpeed * shakeDirection) / 10.0f;
		if (shakeDuration <= 0 && fabsf(shakeOffset) < fabsf(step)) shakeOffset = 0.0f;
		else shakeOffset += step;
		if (shakeOffset > (float)(shakePower * 2)) shakeDirection = -1;
		else if (shakeOffset < (float)(-shakePower * 2)) shakeDirection = 1;
		if (shakeDuration > 0) shakeDuration--;
		else shakeDuration = 0;
	}
	if (state->has_item(1)) {   // lightbulb
		if (++bulbLightTimer > 120) bulbLightTimer = 120;
	} else if (--bulbLightTimer < 0) {
		bulbLightTimer = 0;
	}
	if (screenToneShiftTotalTime > 0) {
		screenToneShiftTimer++;
		if (screenToneShiftTimer >= screenToneShiftTotalTime) {
			screenToneShiftTimer = 0;
			screenToneShiftTotalTime = 0;
			screenTone = targetScreenTone;
		} else {
			float t = (float)screenToneShiftTimer / (float)screenToneShiftTotalTime;
			screenTone = startScreenTone * (1.0f - t) + targetScreenTone * t;
		}
	}
	if (scrollDistanceRemaining > 0) {
		int step = 16 * (2 << scrollSpeed);
		switch (scrollDirection) {
		case Direction::Up: scrollOffset.y -= step; break;
		case Direction::Down: scrollOffset.y += step; break;
		case Direction::Left: scrollOffset.x -= step; break;
		case Direction::Right: scrollOffset.x += step; break;
		default: break;
		}
		scrollDistanceRemaining -= step;
	}
	if (totalChangeMapTime > 0) {
		changeMapTimer++;
		if (changeMapTimer >= totalChangeMapTime) {
			changeMapTimer = 0;
			totalChangeMapTime = 0;
		}
	}
	HashEntities();
	// Index loop: entities may be appended during updates.
	for (size_t i = 0; i < entities.size(); i++) {
		Entity *e = entities[i].get();
		e->Update();
		if (wasMapChangedDuringUpdate) break;   // an event changed the map
		if (Wrapping) {
			Vec2 p = e->GetPos();
			int mw = mapSize.x * tileSize.x * 256, mh = mapSize.y * tileSize.y * 256;
			if (p.x < 0) p.x += mw; else if (p.x > mw) p.x -= mw;
			if (p.y < 0) p.y += mh; else if (p.y > mh) p.y -= mh;
			e->SetPos(p);
		}
		if (e->Active()) {
			if (!e->IgnoreMapCollision()) MapCollision(e);
			if (!e->IgnoreNpcCollision()) HashedEntityCollision(e);
		}
		UpdateEntityHeight(e);
		if (e->KillEntityAfterUpdate) entitiesToKill.push_back(e);
	}
	for (Entity *e : entitiesToKill) RemoveEntity(e);
	entitiesToKill.clear();
	std::vector<Entity *> spawn;
	spawn.swap(entitiesToSpawn);
	for (Entity *e : spawn) AddEntity(e);
	UpdateCamera();
	UpdateCommonEvents();
	for (TileSet &ts : tilesets) ts.Update();
	if (panorama) panorama->Update();
	if (fog) fog->Update();
}

// ---------------------------------------------------------------- entities
Entity *TileMap::GetEntityByID(int id) const {
	auto it = entitiesById.find(id);
	return it == entitiesById.end() ? nullptr : it->second;
}

void TileMap::RemoveEntity(Entity *e) {
	auto it = std::find_if(entities.begin(), entities.end(), [e](const std::unique_ptr<Entity> &u) { return u.get() == e; });
	if (it == entities.end()) return;
	auto byId = entitiesById.find(e->GetID());
	if (byId != entitiesById.end()) entitiesById.erase(byId);
	entitiesByHeight.erase(e->sortedNode);
	if (!e->NeverHash) RemoveEntityFromHash(e);
	retired.push_back(std::move(*it));
	entities.erase(it);
}

const std::vector<std::string> *TileMap::GetStepSounds(Vec2 px, float *volume) const {
	int x = px.x / tileSize.x, y = px.y / tileSize.y;
	*volume = 1.0f;
	if (x < 0 || x >= mapSize.x || y < 0 || y >= mapSize.y || terrainTiles.empty()) return nullptr;
	uint8_t b = terrainTiles[x + y * mapSize.x];
	if (b == 0) return nullptr;
	if (hasStepOverride) return b - 1 < (int)overrideStepSounds.size() ? &overrideStepSounds[b - 1] : nullptr;
	if (!curInfo || b - 1 >= (int)curInfo->step_sounds.size()) return nullptr;
	if (b - 1 < (int)curInfo->step_volumes.size()) *volume = curInfo->step_volumes[b - 1];
	return &curInfo->step_sounds[b - 1];
}

void TileMap::MoveFootsplashesRelative(int x, int y) {
	for (auto &e : entities) {
		Entity *f = e.get();
		if (!f->IsFootsplash()) continue;
		Vec2 p = f->GetPos();
		f->SetPos(Vec2(p.x + x * tileSize.x * 256, p.y + y * tileSize.y * 256));
	}
}

void TileMap::MovePlayerRelative(int x, int y) {
	Vec2 p = player->GetPos();
	p.x += x * tileSize.x * 256;
	p.y += y * tileSize.y * 256;
	player->SetPos(p);
	UpdateCamera();
}

float TileMap::GetPlayerXTile() const { return (float)player->GetPixelPos().x / (float)tileSize.x - 0.5f; }
float TileMap::GetPlayerYTile() const { return (float)player->GetPixelPos().y / (float)tileSize.y - 0.5f; }

void TileMap::ReleaseRetired() {
	if (retired.empty()) return;
	auto referenced = [&](Entity *e) {
		if (!e) return false;
		for (auto &r : retired) if (r.get() == e) return true;
		return false;
	};
	if (mainEventRunner && referenced(mainEventRunner->TriggeringEntity())) return;
	for (auto &pr : parallelEventRunners)
		if (referenced(pr.second->TriggeringEntity())) return;
	for (auto &e : entities)
		if (referenced(e->ReferencedEntity())) return;
	retired.clear();
}

void TileMap::DrawEventRunners() {
	if (mainEventRunner) mainEventRunner->Draw();
	for (auto &pr : parallelEventRunners) pr.second->Draw();
}

void TileMap::DrawMapTransition() {
	if (totalChangeMapTime > 0) {
		float alpha = (float)(totalChangeMapTime - changeMapTimer) / (float)totalChangeMapTime;
		gm::draw_captured_window(alpha);
	}
}

void TileMap::ChangeMap(int mapId, float x, float y, float time, Direction dir) {
	Direction prev = player->GetDirection();
	changeMapTimer = 0;
	totalChangeMapTime = (int)(time * 60.0f);
	gm::capture_window();   // SetUpMapTransitionFrame: the last presented frame
	LoadMap(mapId, x, y, dir == Direction::None ? prev : dir);
	UpdateCamera();
}

void TileMap::ExecuteMapTransition(const std::string &name) {
	// SpecialTransition (transition-pattern wipe) is approximated by a crossfade.
	static bool warned;
	if (!warned) { warned = true; sys::log("map transition '%s': using crossfade\n", name.c_str()); }
	changeMapTimer = 0;
	totalChangeMapTime = 40;
}

void TileMap::ScrollMap(int direction, int distance, int speed) {
	scrollDirection = (Direction)direction;
	scrollDistanceRemaining = distance * 16 * 256;
	scrollSpeed = speed;
}

long long TileMap::FramesSinceToneChange() const { return state->play_frames - toneChangeFrame; }

void TileMap::SetScreenTone(GameTone tone, int shiftTime) {
	toneChangeFrame = state->play_frames;
	screenToneShiftTimer = 0;
	if (shiftTime <= 0) {
		screenToneShiftTotalTime = 0;
		screenTone = tone;
	} else {
		startScreenTone = screenTone;
		targetScreenTone = tone;
		screenToneShiftTotalTime = shiftTime;
	}
}

void TileMap::ScreenShake(int power, int speed, int duration, bool) {
	shakePower = power;
	shakeSpeed = speed;
	shakeDuration = duration;
	// Controller vibration: later (DS3 actuators via ioPadSetActDirect).
}

void TileMap::SetPanorama(const std::string &name) {
	if (name.empty()) return;
	std::string t = "panoramas/" + name;
	panorama.reset(new Panorama(t, gm::texture_size(t.c_str())));
}

void TileMap::SetFog(const std::string &name, int hue, float opacity, gfx::Blend blend, int sx, int sy) {
	if (name.empty()) return;
	std::string t = "fogs/" + name;
	fog.reset(new Fog(t, gm::texture_size(t.c_str()), hue, opacity, blend, sx, sy));
}

Vec2 TileMap::GetMaxCamPos() const {
	Vec2 r(mapSize.x * tileSize.x - 320, mapSize.y * tileSize.y - 240);
	if (r.x < 0) r.x = 0;
	if (r.y < 0) r.y = 0;
	return r;
}

void TileMap::UpdateCamera() {
	oldCamPos = camPos;
	if (state->flag(100)) return;
	Vec2 p = camFocusEntity->GetPixelPos();
	camPos.x = p.x - 160 + scrollOffset.x / 256;
	camPos.y = p.y - 120 + scrollOffset.y / 256;
	camPos.x += (int)rintf(shakeOffset) / 2;
	if (state->flag(98)) {
		if (camPos.x < 0) camPos.x = 0;
		if (camPos.y < 0) camPos.y = 0;
		Vec2 m = GetMaxCamPos();
		if (camPos.x > m.x) camPos.x = m.x;
		if (camPos.y > m.y) camPos.y = m.y;
	}
}

// ---------------------------------------------------------------- draw (TileMapManager.Draw)
void TileMap::DrawEntity(Entity *e, GameTone tone) {
	if (debugLayers == 1 || debugLayers == 3) return;
	e->Draw(camPos, tone);
	if (Wrapping) {
		Vec2 a = camPos, b = camPos;
		a.x -= mapSize.x * tileSize.x;
		b.x += mapSize.x * tileSize.x;
		e->Draw(a, tone);
		e->Draw(b, tone);
	}
}

void TileMap::Draw() {
	float bulb = (float)bulbLightTimer / 120.0f;
	GameTone tone = ambientTone * (1.0f - bulb) + screenTone;
	int n2 = camPos.x / tileSize.x, n3 = camPos.y / tileSize.y;
	int n4 = (camPos.x + 320) / tileSize.x + 1, n5 = (camPos.y + 240) / tileSize.y + 1;
	if (!Wrapping) {
		if (n2 < 0) n2 = 0;
		if (n3 < 0) n3 = 0;
		if (n4 >= mapSize.x) n4 = mapSize.x - 1;
		if (n5 >= mapSize.y) n5 = mapSize.y - 1;
	} else {
		if (n2 <= 0) n2--;
		if (n3 <= 0) n3--;
	}
	int x = (camPos.x - n2 * tileSize.x) * -1;
	int y = (camPos.y - n3 * tileSize.y) * -1;
	Vec2 zero(x, y);
	auto node = entitiesByHeight.begin();

	if (panorama) panorama->Draw(camPos, player->GetPixelPos(), mapSize, tileSize, tone);
	if (!background.empty()) {
		gm::blit(gm::texture(("panoramas/" + background).c_str()), Vec2(-camPos.x, -camPos.y), 1.0f, 0,
		         gfx::Blend::Normal, 2, tone);
	} else {
		auto base = heightsToLayers.find(0);
		if (base != heightsToLayers.end())
			for (auto &l : base->second) l->Draw(tilesets, camPos, tileSize, Wrapping, tone);
	}

	for (int i = n3; i <= n5 + 6; i++) {
		int row = i;
		if (row < 0) row += mapSize.y; else if (row >= mapSize.y) row -= mapSize.y;
		for (int j = 1; j < 6 && j <= i - n3 && background.empty(); j++) {
			if (i - j > n5) continue;
			if (debugLayers >= 2) break;
			auto hv = heightsToLayers.find(j);
			if (hv == heightsToLayers.end()) continue;
			Vec2 dp = zero;
			dp.y -= j * tileSize.y;
			for (auto &l : hv->second) {
				for (int k = n2; k <= n4; k++) {
					int col = k;
					if (col < 0) col += mapSize.x; else if (col >= mapSize.x) col -= mapSize.x;
					l->DrawTile(dp, col, row, tilesets, j, tone);
					dp.x += tileSize.x;
				}
				dp.x = x;
			}
		}
		zero.y += tileSize.y;
		while (node != entitiesByHeight.end() && (*node)->GetPixelBottom() <= (i + 1) * tileSize.y) {
			DrawEntity(*node, tone);
			++node;
		}
	}
	while (node != entitiesByHeight.end()) { DrawEntity(*node, tone); ++node; }
	if (fog) fog->Draw(camPos, tone);
	if (hasFireflies)
		if (const gfx::Texture *ff = gm::texture("pictures/firefly"))
			for (const Firefly &f : fireflies)
				gm::blit_scaled(ff, Vec2(f.pos.x / 256, f.pos.y / 256), f.src, f.size, f.size, f.alpha, 0, gfx::Blend::Additive);

	if (bulb > 0.0f) {
		const gfx::Texture *lm = gm::texture("lightmaps/bulb");
		if (lm) gm::blit(lm, Vec2((320 - (int)lm->w) / 2 - (int)(shakeOffset / 2.0f), 0), bulb);
	}
}

Vec2 TileMap::FindOpenTileNear(int cx, int cy) const {
	auto base = heightsToLayers.find(0);
	auto has_floor = [&](int x, int y) {
		if (base == heightsToLayers.end()) return false;
		for (auto &l : base->second)
			if (l->GetTileId(x, y) != 0) return true;
		return false;
	};
	for (int r = 0; r < std::max(mapSize.x, mapSize.y); r++)
		for (int dy = -r; dy <= r; dy++)
			for (int dx = -r; dx <= r; dx++) {
				if (std::max(iabs(dx), iabs(dy)) != r) continue;
				int x = cx + dx, y = cy + dy;
				if (x < 0 || y < 0 || x >= mapSize.x || y >= mapSize.y) continue;
				if (!IsTileSolid(x, y) && has_floor(x, y)) return Vec2(x, y);
			}
	return Vec2(cx, cy);
}

// ---------------------------------------------------------------- save data

cJSON *TileMap::GetSaveData() const {
	cJSON *o = cJSON_CreateObject();
	cJSON_AddNumberToObject(o, "playerTileX", GetPlayerXTile());
	cJSON_AddNumberToObject(o, "playerTileY", GetPlayerYTile());
	cJSON_AddNumberToObject(o, "playerDirection", (int)player->GetDirection());
	cJSON_AddStringToObject(o, "playerSheet", player->GetNPCSheet().c_str());
	cJSON_AddNumberToObject(o, "currentMap", currentMapId);
	cJSON *ents = cJSON_AddArrayToObject(o, "entitySaveDatas");
	for (auto &kv : entitiesById)
		if (kv.second->GetID() >= 0) cJSON_AddItemToArray(ents, kv.second->GetSaveData());
	cJSON *tone = cJSON_AddObjectToObject(o, "mapAmbientTone");
	cJSON_AddNumberToObject(tone, "r", ambientTone.r);
	cJSON_AddNumberToObject(tone, "g", ambientTone.g);
	cJSON_AddNumberToObject(tone, "b", ambientTone.b);
	cJSON_AddStringToObject(o, "mapBackground", background.c_str());
	cJSON_AddBoolToObject(o, "hasFireflyParticles", hasFireflies);
	if (mainEventRunner) cJSON_AddItemToObject(o, "mainEventRunnerData", mainEventRunner->GetSaveData());
	if (panorama) {
		cJSON *p = cJSON_AddObjectToObject(o, "panoramaSaveData");
		cJSON_AddStringToObject(p, "name", panorama->Source().c_str());
		cJSON_AddNumberToObject(p, "X", panorama->Size().x);
		cJSON_AddNumberToObject(p, "Y", panorama->Size().y);
	}
	if (hasStepOverride) {
		cJSON *a = cJSON_AddArrayToObject(o, "overrideStepSounds");
		for (auto &v : overrideStepSounds) {
			cJSON *inner = cJSON_CreateArray();
			for (auto &n : v) cJSON_AddItemToArray(inner, cJSON_CreateString(n.c_str()));
			cJSON_AddItemToArray(a, inner);
		}
	}
	return o;
}

static int jn(const cJSON *o, const char *k, int def) {
	const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
	return cJSON_IsNumber(v) ? (int)v->valuedouble : def;
}

void TileMap::LoadSaveData(const cJSON *o) {
	const cJSON *px = cJSON_GetObjectItemCaseSensitive(o, "playerTileX");
	const cJSON *py = cJSON_GetObjectItemCaseSensitive(o, "playerTileY");
	float x = cJSON_IsNumber(px) ? (float)px->valuedouble : 0, y = cJSON_IsNumber(py) ? (float)py->valuedouble : 0;
	ChangeMap(jn(o, "currentMap", 1), x, y, 0.0f, (Direction)jn(o, "playerDirection", 2));
	// LoadEntitySaveDatas: restore saved entities, drop ones that were gone.
	std::unordered_map<int, const cJSON *> byId;
	const cJSON *e;
	cJSON_ArrayForEach(e, cJSON_GetObjectItemCaseSensitive(o, "entitySaveDatas")) {
		int id = jn(e, "id", -1);
		if (!byId.count(id)) byId[id] = e;
	}
	std::vector<Entity *> gone;
	for (auto &kv : entitiesById) {
		auto it = byId.find(kv.second->GetID());
		if (it != byId.end()) kv.second->LoadSaveData(it->second);
		else if (!kv.second->IsPlayer() && !kv.second->IsFollower()) gone.push_back(kv.second);
	}
	for (Entity *g : gone) RemoveEntity(g);
	const cJSON *sheet = cJSON_GetObjectItemCaseSensitive(o, "playerSheet");
	player->SetNPCSheet(cJSON_IsString(sheet) ? sheet->valuestring : "");
	const cJSON *tone = cJSON_GetObjectItemCaseSensitive(o, "mapAmbientTone");
	if (tone) ambientTone = GameTone(jn(tone, "r", 0), jn(tone, "g", 0), jn(tone, "b", 0));
	if (cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(o, "hasFireflyParticles"))) SpawnFireflyParticles();
	const cJSON *bg = cJSON_GetObjectItemCaseSensitive(o, "mapBackground");
	background = cJSON_IsString(bg) ? bg->valuestring : "";
	const cJSON *pan = cJSON_GetObjectItemCaseSensitive(o, "panoramaSaveData");
	if (cJSON_IsObject(pan)) {
		const cJSON *n = cJSON_GetObjectItemCaseSensitive(pan, "name");
		if (cJSON_IsString(n)) panorama.reset(new Panorama(n->valuestring, Vec2(jn(pan, "X", 0), jn(pan, "Y", 0))));
	}
	const cJSON *run = cJSON_GetObjectItemCaseSensitive(o, "mainEventRunnerData");
	if (cJSON_IsObject(run)) {
		if (mainEventRunner) retiredRunners.push_back(std::move(mainEventRunner));
		mainEventRunner.reset(new EventRunner(this, run));
	}
	const cJSON *steps = cJSON_GetObjectItemCaseSensitive(o, "overrideStepSounds");
	if (cJSON_IsArray(steps)) {
		overrideStepSounds.clear();
		const cJSON *inner;
		cJSON_ArrayForEach(inner, steps) {
			std::vector<std::string> v;
			const cJSON *n;
			cJSON_ArrayForEach(n, inner) if (cJSON_IsString(n)) v.push_back(n->valuestring);
			overrideStepSounds.push_back(v);
		}
		hasStepOverride = true;
	}
}
