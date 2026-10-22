// tilemap.cpp - see tilemap.h. Method bodies follow the decompiled C#
// (TileSet.cs, TileLayer.cs, TileMapManager.cs, MapLine.cs) closely so
// collision and draw order behave identically.
#include "tilemap.h"
#include "player.h"
#include "game_state.h"
#include "event_runner.h"
#include "pictures.h"
#include "../engine/audio.h"
#include "../engine/gm.h"
#include "../engine/xml.h"
#include "../platform/sys.h"
#include "cJSON.h"

#include <algorithm>
#include <math.h>
#include <stdlib.h>
#include <string.h>

// ======================================================================= TileSet
bool TileSet::Read(int firstId, const std::string &fileName) {
	// Maps share a few dozen .tsx files: parse each once.
	static std::unordered_map<std::string, TileSet> parsed;
	auto pc = parsed.find(fileName);
	if (pc != parsed.end()) {
		*this = pc->second;
		firstTileId = firstId;
		ResolveTexture();
		return true;
	}
	if (!ReadTsx(fileName)) return false;
	TileSet proto = *this;
	proto.tex = nullptr;
	parsed.emplace(fileName, proto);
	firstTileId = firstId;
	ResolveTexture();
	return true;
}

bool TileSet::ReadTsx(const std::string &fileName) {
	size_t num = fileName.find("tilesets/");
	autotile = false;
	if (num == std::string::npos) {
		num = fileName.find("autotiles/");
		autotile = true;
		if (num == std::string::npos) {
			sys::log("tileset '%s' is not in the tilesets folder!\n", fileName.c_str());
			return false;
		}
	}
	std::string rel = "gamedata/" + fileName.substr(num);
	uint8_t *data;
	size_t size;
	if (!sys::read_file(rel.c_str(), &data, &size)) {
		sys::log("cannot read %s\n", rel.c_str());
		return false;
	}
	std::string err;
	auto root = xml::parse((const char *)data, &err);
	free(data);
	if (!root) { sys::log("%s: %s\n", rel.c_str(), err.c_str()); return false; }
	tileSize = Vec2(root->attr_int("tilewidth", 16), root->attr_int("tileheight", 16));
	totalTiles = root->attr_int("tilecount", 0);
	tileColumns = root->attr_int("columns", 1);
	if (tileColumns <= 0) tileColumns = 1;
	const xml::Node *img = root->child("image");
	imageFileName = img ? img->attr("source", "") : "";
	size_t c = imageFileName.find("Content/");
	if (c == std::string::npos) {
		sys::log("image '%s' in tileset '%s' is not in the Content folder!\n", imageFileName.c_str(), rel.c_str());
	} else {
		imageFileName = imageFileName.substr(c + 8);
		size_t png = imageFileName.find(".png");
		if (png != std::string::npos) imageFileName = imageFileName.substr(0, png);
	}
	ResolveTexture();
	return true;
}

void TileSet::ResolveTexture() {
	tex = gm::texture(imageFileName.c_str());
	animated = false;
	if (autotile && tex && tex->h > 96) {
		animated = true;
		animAreaRows = 6;
		animFrames = (int)tex->h / 96;
		animFrameTime = 16;
		animTimer = 0;
		frameIndex = 0;
	}
}

void TileSet::DrawTile(Vec2 drawPos, int tileID, GameTone tone, float alpha) const {
	int num = tileID - firstTileId + frameIndex * animAreaRows * tileColumns;
	if (num < 0) return;
	int x = num % tileColumns * tileSize.x;
	int y = num / tileColumns * tileSize.y;
	gm::blit(tex, drawPos, Rect(x, y, tileSize.x, tileSize.y), alpha, 0, gfx::Blend::Normal, 2, tone);
}

void TileSet::Update() {
	if (!animated) return;
	if (++animTimer > animFrameTime) {
		animTimer = 0;
		if (++frameIndex >= animFrames) frameIndex = 0;
	}
}

// ======================================================================= TileLayer
static const TileSet &tileset_for(const std::vector<TileSet> &tilesets, int id) {
	const TileSet *ts = &tilesets[0];
	for (const TileSet &t : tilesets) {
		if (t.GetFirstTileID() > id) break;
		ts = &t;
	}
	return *ts;
}

TileLayer::TileLayer(Vec2 mapSize) : tiles(mapSize.x * mapSize.y, 0), layerSize(mapSize) {}

TileLayer::TileLayer(Vec2 mapSize, const std::string &csv, Vec2 ls)
	: tiles(mapSize.x * mapSize.y, 0), layerSize(ls) {
	const char *p = csv.c_str();
	size_t n = tiles.size();
	for (size_t i = 0; i < n && *p; i++) {
		while (*p && (*p < '0' || *p > '9') && *p != '-') p++;
		tiles[i] = (int16_t)strtol(p, (char **)&p, 10);
	}
}

int16_t TileLayer::GetRPGMakerTileId(int x, int y, const std::vector<TileSet> &tilesets) const {
	int16_t tileId = GetTileId(x, y);
	if (tileId == 0) return 0;
	int num2 = 0;
	bool first = true;
	const TileSet *ts = &tilesets[0];
	for (const TileSet &t : tilesets) {
		if (first) { first = false; continue; }
		if (t.GetFirstTileID() > tileId) break;
		num2++;
		ts = &t;
	}
	return (int16_t)(tileId - ts->GetFirstTileID() + num2 * 48);
}

void TileLayer::DrawTile(Vec2 drawPos, int xIndex, int yIndex, const std::vector<TileSet> &tilesets,
                         int height, GameTone tone) const {
	yIndex -= height;
	if (yIndex < 0) yIndex += layerSize.y;
	int num = tiles[xIndex + yIndex * layerSize.x];
	if (num == 0) return;
	tileset_for(tilesets, num).DrawTile(drawPos, num, tone);
}

void TileLayer::Draw(const std::vector<TileSet> &tilesets, Vec2 camPos, Vec2 tileSize, bool wrapping,
                     GameTone tone, float alpha) const {
	int num = camPos.x / tileSize.x, num2 = camPos.y / tileSize.y;
	int num3 = (camPos.x + 320) / tileSize.x + 1, num4 = (camPos.y + 240) / tileSize.y + 1;
	if (!wrapping) {
		if (num < 0) num = 0;
		if (num2 < 0) num2 = 0;
		if (num3 >= layerSize.x) num3 = layerSize.x - 1;
		if (num4 >= layerSize.y) num4 = layerSize.y - 1;
	} else {
		if (num <= 0) num--;
		if (num2 <= 0) num2--;
	}
	int x = (camPos.x - num * tileSize.x) * -1;
	int y = (camPos.y - num2 * tileSize.y) * -1;
	Vec2 zero(x, y);
	for (int i = num2; i <= num4; i++) {
		int r = i;
		if (r < 0) r += layerSize.y; else if (r >= layerSize.y) r -= layerSize.y;
		for (int j = num; j <= num3; j++) {
			int c = j;
			if (c < 0) c += layerSize.x; else if (c >= layerSize.x) c -= layerSize.x;
			int id = tiles[c + r * layerSize.x];
			if (id != 0) tileset_for(tilesets, id).DrawTile(zero, id, tone, alpha);
			zero.x += tileSize.x;
		}
		zero.y += tileSize.y;
		zero.x = x;
	}
}

// ======================================================================= MapLine
struct TileMap::MapLine {
	enum Slope { Floor, Ceiling, EastWall, WestWall } slope = Floor;
	Vec2 startPoint, endPoint;
	int id;

	MapLine(Vec2 s, Vec2 e, int lineID) : startPoint(s), endPoint(e), id(lineID) {
		int dx = endPoint.x - startPoint.x, dy = endPoint.y - startPoint.y;
		if (dx == 0) { slope = dy > 0 ? WestWall : EastWall; return; }
		float m = (float)dy / (float)dx;
		if (m <= 1.0 && m >= -1.0) slope = dx > 0 ? Floor : Ceiling;
		else slope = dy > 0 ? WestWall : EastWall;
	}

	void hashIntoTile(int index, std::unordered_map<int, std::vector<MapLine *>> &h) { h[index].push_back(this); }

	void hashIntoTiles(std::unordered_map<int, std::vector<MapLine *>> &h, Vec2 ts, Vec2 ms) {
		int dx = endPoint.x - startPoint.x, dy = endPoint.y - startPoint.y;
		ts.x *= 256; ts.y *= 256;
		float m;
		int x, y, y2, n5, n6;
		if (iabs(dx) >= iabs(dy)) {
			m = (float)dy / (float)dx;
			int n4;
			if (dx > 0) { n4 = endPoint.x + ts.x / 2; x = startPoint.x; y = endPoint.y; y2 = startPoint.y; }
			else { n4 = startPoint.x + ts.x / 2; x = endPoint.x; y = startPoint.y; y2 = endPoint.y; }
			(void)y;
			n5 = x / ts.x; n6 = y2 / ts.y;
			hashIntoTile(n6 * ms.x + n5, h);
			int i = x + 1;
			if (i % ts.x != 0) i = (i / ts.x + 1) * ts.x;
			for (; i <= n4; i += ts.x) {
				int n7 = i / ts.x;
				int n8 = (int)((float)(i - x) * m + (float)y2) / ts.y;
				hashIntoTile(n8 * ms.x + n7, h);
				if (n8 != n6) hashIntoTile(n8 * ms.x + (n7 - 1), h);
				n6 = n8; n5 = n7;
			}
			return;
		}
		m = (float)dx / (float)dy;
		if (dy > 0) { x = startPoint.x; y = endPoint.y + ts.y / 2; y2 = startPoint.y; }
		else { x = endPoint.x; y = startPoint.y + ts.y / 2; y2 = endPoint.y; }
		n5 = x / ts.x; n6 = y2 / ts.y;
		hashIntoTile(n6 * ms.x + n5, h);
		int j = y2 + 1;
		if (j % ts.y != 0) j = (j / ts.y + 1) * ts.y;
		for (; j <= y; j += ts.y) {
			int n8 = j / ts.y;
			int n7 = (int)((float)(j - y2) * m + (float)x) / ts.x;
			hashIntoTile(n8 * ms.x + n7, h);
			if (n7 != n5) hashIntoTile((n8 - 1) * ms.x + n7, h);
			n6 = n8; n5 = n7;
		}
	}

	void Collision(Entity *e) {
		Vec2 pos = e->GetPos(), oldPos = e->GetOldPos();
		Rect cr = e->GetCollisionRect();
		Vec2 z, z2, z3, z4;
		int n1, n2, n3, n4, n5;
		switch (slope) {
		case Floor:
			n1 = pos.x + cr.x * 256; n2 = pos.x + (cr.x + cr.w) * 256;
			n3 = pos.y + cr.y * 256; n4 = pos.y + (cr.y + cr.h) * 256; n5 = oldPos.y + (cr.y + cr.h) * 256;
			z3 = pos; z4 = oldPos; z = startPoint; z2 = endPoint;
			break;
		case Ceiling:
			n1 = pos.x + cr.x * 256; n2 = pos.x + (cr.x + cr.w) * 256;
			n3 = -(pos.y + (cr.y + cr.h) * 256); n4 = -(pos.y + cr.y * 256); n5 = -(oldPos.y + cr.y * 256);
			z3 = Vec2(pos.x, -pos.y); z4 = Vec2(oldPos.x, -oldPos.y);
			z = Vec2(endPoint.x, -endPoint.y); z2 = Vec2(startPoint.x, -startPoint.y);
			break;
		case EastWall:
			n1 = -(pos.y + (cr.y + cr.h) * 256); n2 = -(pos.y + cr.y * 256);
			n3 = pos.x + cr.x * 256; n4 = pos.x + (cr.x + cr.w) * 256; n5 = oldPos.x + (cr.x + cr.w) * 256;
			z3 = Vec2(-pos.y, pos.x); z4 = Vec2(-oldPos.y, oldPos.x);
			z = Vec2(-startPoint.y, startPoint.x); z2 = Vec2(-endPoint.y, endPoint.x);
			break;
		default: // WestWall
			n1 = pos.y + cr.y * 256; n2 = pos.y + (cr.y + cr.h) * 256;
			n3 = -(pos.x + (cr.x + cr.w) * 256); n4 = -(pos.x + cr.x * 256); n5 = -(oldPos.x + cr.x * 256);
			z3 = Vec2(pos.y, -pos.x); z4 = Vec2(oldPos.y, -oldPos.x);
			z = Vec2(startPoint.y, -startPoint.x); z2 = Vec2(endPoint.y, -endPoint.x);
			break;
		}
		if (n2 <= z.x || n1 >= z2.x) return;
		float m = (float)(z2.y - z.y) / (float)(z2.x - z.x);
		int n7 = (int)((float)(z3.x - z.x) * m + (float)z.y);
		int n8 = (int)((float)(z4.x - z.x) * m + (float)z.y);
		if (n4 > n7 && n3 < n7 && n5 <= n8 + 512) {
			int n9 = n7 - n4;
			double ang = atan2((double)(z2.y - z.y), (double)(z2.x - z.x));
			int n11 = (int)(sin(ang) * n9), n12 = (int)(cos(ang) * n9);
			switch (slope) {
			case Floor:    e->SetCollisionDirection(Direction::Down);  e->SetVel(Vec2()); e->SetPos(Vec2(pos.x - n11, pos.y + n12)); break;
			case Ceiling:  e->SetCollisionDirection(Direction::Up);    e->SetVel(Vec2()); e->SetPos(Vec2(pos.x - n11, pos.y - n12)); break;
			case EastWall: e->SetCollisionDirection(Direction::Right); e->SetVel(Vec2()); e->SetPos(Vec2(pos.x + n12, pos.y + n11)); break;
			case WestWall: e->SetCollisionDirection(Direction::Right); e->SetVel(Vec2()); e->SetPos(Vec2(pos.x - n12, pos.y - n11)); break;
			}
		}
	}
};

// ======================================================================= TileMap
TileMap::TileMap(GameState *s) : state(s) {}
TileMap::~TileMap() = default;   // out of line: EventRunner is incomplete in the header

static std::vector<uint8_t> json_bytes(const cJSON *arr) {
	std::vector<uint8_t> v;
	const cJSON *it;
	cJSON_ArrayForEach(it, arr) v.push_back((uint8_t)it->valueint);
	return v;
}

bool TileMap::LoadGlobalData() {
	uint8_t *data;
	size_t size;
	if (!sys::read_file("gamedata/oneshot_tilesets.json", &data, &size)) {
		sys::log("cannot read oneshot_tilesets.json\n");
		return false;
	}
	cJSON *root = cJSON_ParseWithLength((const char *)data, size);
	free(data);
	if (!root) return false;
	const cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "tilesets"), *t;
	cJSON_ArrayForEach(t, arr) {
		TilesetInfo ti;
		ti.id = cJSON_GetObjectItemCaseSensitive(t, "id")->valueint;
		const cJSON *pn = cJSON_GetObjectItemCaseSensitive(t, "panorama_name");
		if (cJSON_IsString(pn)) ti.panorama_name = pn->valuestring;
		ti.passages = json_bytes(cJSON_GetObjectItemCaseSensitive(t, "passages"));
		ti.priorities = json_bytes(cJSON_GetObjectItemCaseSensitive(t, "priorities"));
		ti.terrain_tags = json_bytes(cJSON_GetObjectItemCaseSensitive(t, "terrain_tags"));
		const cJSON *ss = cJSON_GetObjectItemCaseSensitive(t, "step_sounds"), *e;
		cJSON_ArrayForEach(e, ss) {
			std::vector<std::string> names;
			const cJSON *nm;
			cJSON_ArrayForEach(nm, e) if (cJSON_IsString(nm)) names.push_back(nm->valuestring);
			ti.step_sounds.push_back(names);
		}
		const cJSON *sv = cJSON_GetObjectItemCaseSensitive(t, "step_volumes");
		cJSON_ArrayForEach(e, sv) ti.step_volumes.push_back(cJSON_IsNumber(e) ? (float)e->valuedouble : 1.0f);
		tilesetInfos[ti.id] = std::move(ti);
	}
	cJSON_Delete(root);
	sys::log("loaded %d tileset infos\n", (int)tilesetInfos.size());
	std::vector<CommonEvent> ces;
	if (load_common_events("gamedata/oneshot_common_events.json", ces)) {
		for (auto &ce : ces) commonEvents[ce.id] = std::move(ce);
		sys::log("loaded %d common events\n", (int)commonEvents.size());
	} else {
		sys::log("WARNING: gamedata/oneshot_common_events.json missing - common events disabled\n");
	}
	return true;
}

static uint8_t at(const std::vector<uint8_t> &v, int i) { return (i >= 0 && i < (int)v.size()) ? v[i] : 0; }

bool TileMap::LoadMap(int mapId, float playerTileX, float playerTileY, Direction facing) {
	uint64_t t0 = sys::time_us();
	bool hadMap = currentMapId != 0;
	heightsToLayers.clear();
	tilesets.clear();
	entitiesByHeight.clear();
	hashedEntities.clear();
	entitiesById.clear();
	mapLines.clear();
	hashedMapLines.clear();
	// Keep the player object across maps (C# reuses it). The rest are retired,
	// not deleted: this can run from inside an entity's Update or an event
	// that still refers to its triggering entity.
	wasMapChangedDuringUpdate = true;
	std::unique_ptr<Entity> keepPlayer;
	for (auto &e : entities) {
		if (e.get() == player) keepPlayer = std::move(e);
		else retired.push_back(std::move(e));
	}
	entities.clear();
	entitiesToKill.clear();
	for (Entity *e : entitiesToSpawn) delete e;
	entitiesToSpawn.clear();
	eventFile.reset();
	scrollDistanceRemaining = 0;
	scrollSpeed = 0;
	scrollDirection = Direction::None;
	background.clear();
	overrideStepSounds.clear();
	hasStepOverride = false;
	panorama.reset();
	fog.reset();
	if (hadMap) gm::new_generation(gm::Game);   // the PC clears the Game cache here (see gm.h)
	entityIDcounter = 0;
	currentMapId = mapId;
	state->map_id = mapId;
	ambientTone = GameTone();
	scrollOffset = Vec2();
	Wrapping = false;

	char rel[128];
	snprintf(rel, sizeof(rel), "gamedata/maps/map%d.tmx", mapId);
	// On failure keep the player object alive (events and the camera still
	// point at it) and leave a message for the window to show.
	auto fail = [&](const std::string &msg) {
		sys::log("ERROR: %s\n", msg.c_str());
		loadError = msg;
		if (keepPlayer) entities.push_back(std::move(keepPlayer));
		return false;
	};
	uint8_t *data;
	size_t size;
	if (!sys::read_file(rel, &data, &size)) return fail(std::string("cannot read ") + rel);
	uint64_t tRead = sys::time_us();
	std::string err;
	auto root = xml::parse((const char *)data, &err);
	free(data);
	uint64_t tXml = sys::time_us();
	if (!root) return fail(std::string(rel) + ": " + err);

	mapSize = Vec2(root->attr_int("width"), root->attr_int("height"));
	tileSize = Vec2(root->attr_int("tilewidth", 16), root->attr_int("tileheight", 16));
	hashTileSize = tileSize;
	hashMapSize = Vec2(mapSize.x + 1, mapSize.y + 1);
	currentTilesetId = 0;
	if (const xml::Node *props = root->child("properties"))
		for (auto &p : props->children)
			if (p->name == "property" && strcmp(p->attr("name", ""), "tileset_id") == 0)
				currentTilesetId = p->attr_int("value");
	auto ti = tilesetInfos.find(currentTilesetId);
	if (ti == tilesetInfos.end()) return fail(std::string(rel) + ": unknown tileset_id " + std::to_string(currentTilesetId));
	curInfo = &ti->second;

	std::vector<std::unique_ptr<TileLayer>> list;
	for (auto &n : root->children) {
		if (n->name == "tileset") {
			TileSet ts;
			ts.Read(n->attr_int("firstgid"), n->attr("source", ""));
			tilesets.push_back(ts);
		} else if (n->name == "layer") {
			const xml::Node *d = n->child("data");
			if (!d || strcmp(d->attr("encoding", ""), "csv") != 0) { sys::log("map %d: non-CSV layer\n", mapId); continue; }
			list.push_back(std::make_unique<TileLayer>(mapSize, d->text, Vec2(n->attr_int("width"), n->attr_int("height"))));
		} else if (n->name == "objectgroup") {
			std::string gname = n->attr("name", "");
			std::transform(gname.begin(), gname.end(), gname.begin(), ::tolower);
			if (gname != "poly_collision") continue;
			for (auto &o : n->children) {
				const xml::Node *poly = o->child("polygon");
				if (!poly) continue;
				float ox = (float)o->attr_double("x"), oy = (float)o->attr_double("y");
				std::vector<Vec2> pts;
				const char *s = poly->attr("points", "");
				while (*s) {
					float px = strtof(s, (char **)&s);
					if (*s == ',') s++;
					float py = strtof(s, (char **)&s);
					pts.push_back(Vec2((int)(px + ox) * 256, (int)(py + oy) * 256));
					while (*s == ' ') s++;
				}
				for (size_t j = 0; j < pts.size(); j++)
					mapLines.push_back(std::make_unique<MapLine>(pts[j], pts[(j + 1) % pts.size()], (int)mapLines.size()));
			}
		}
	}
	if (tilesets.empty()) { sys::log("map %d: no tilesets\n", mapId); return false; }
	uint64_t tTilesets = sys::time_us();
	SetPanorama(curInfo->panorama_name);
	uint64_t tPanorama = sys::time_us();

	const int W = mapSize.x, H = mapSize.y;
	counterTiles.assign(W * H, false);
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++)
			for (auto &l : list)
				if ((at(curInfo->passages, l->GetRPGMakerTileId(x, y, tilesets)) & 0x80) == 128) { counterTiles[x + y * W] = true; break; }

	collisionTiles.assign(W * H, false);
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++) {
			bool solid = false;
			int layerNo = 1;
			for (auto it = list.rbegin(); it != list.rend(); ++it, ++layerNo) {   // reversed, as in C#
				int rid = (*it)->GetRPGMakerTileId(x, y, tilesets);
				if (rid >= 48 || layerNo >= 3) {
					if ((at(curInfo->passages, rid) & 0xF) == 15) { solid = true; break; }
					if (at(curInfo->priorities, rid) == 0) { solid = false; break; }
				}
			}
			collisionTiles[x + y * W] = solid;
		}

	terrainTiles.assign(W * H, 0);
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++)
			for (auto &l : list) {
				uint8_t t = at(curInfo->terrain_tags, l->GetRPGMakerTileId(x, y, tilesets));
				if (t) terrainTiles[x + y * W] = t;
			}

	// Split tiles with priority > 0 into per-height layers.
	for (auto &l : list) {
		for (int y = 0; y < H; y++)
			for (int x = 0; x < W; x++) {
				int p = at(curInfo->priorities, l->GetRPGMakerTileId(x, y, tilesets));
				if (p == 0) continue;
				auto &value = heightsToLayers[p];
				TileLayer *target = nullptr;
				for (auto &hl : value)
					if (hl->GetTileId(x, y) == 0) { target = hl.get(); break; }
				if (!target) {
					value.push_back(std::make_unique<TileLayer>(mapSize));
					target = value.back().get();
				}
				target->SetTileId(x, y, l->GetTileId(x, y));
				l->SetTileId(x, y, 0);
			}
	}
	heightsToLayers[0] = std::move(list);

	for (auto &ml : mapLines) ml->hashIntoTiles(hashedMapLines, tileSize, mapSize);

	uint64_t tLayers = sys::time_us();
	snprintf(rel, sizeof(rel), "gamedata/maps/events_map%d.json", mapId);
	eventFile.reset();
	for (auto it = eventCache.begin(); it != eventCache.end(); ++it)
		if (it->first == mapId) {
			eventFile = it->second;
			eventCache.erase(it);
			break;
		}
	if (!eventFile) eventFile = load_event_file(rel);
	if (eventFile) {
		eventCache.emplace_front(mapId, eventFile);
		while (eventCache.size() > 6) eventCache.pop_back();
	}
	uint64_t tEvents = sys::time_us();
	if (eventFile && !eventFile->events.empty()) {
		entityIDcounter = 0;
		for (auto &e : eventFile->events) entityIDcounter = std::max(entityIDcounter, e.id);
	} else {
		entityIDcounter = 10;
	}
	if (eventFile)
		for (auto &ed : eventFile->events) AddEntity(new Entity(this, state, &ed));

	uint64_t tEntities = sys::time_us();
	snprintf(rel, sizeof(rel), "gamedata/maps/music_map%d.json", mapId);
	{
		uint8_t *md;
		size_t ms;
		if (sys::read_file(rel, &md, &ms)) {
			cJSON *mj = cJSON_ParseWithLength((const char *)md, ms);
			free(md);
			const cJSON *ap = cJSON_GetObjectItemCaseSensitive(mj, "autoplay_bgm");
			const cJSON *bgm = cJSON_GetObjectItemCaseSensitive(mj, "bgm");
			if (cJSON_IsTrue(ap) && bgm) {
				const cJSON *n = cJSON_GetObjectItemCaseSensitive(bgm, "name");
				const cJSON *v = cJSON_GetObjectItemCaseSensitive(bgm, "volume");
				const cJSON *p = cJSON_GetObjectItemCaseSensitive(bgm, "pitch");
				audio::queue_song(cJSON_IsString(n) ? n->valuestring : "", 0.25f,
				                  (cJSON_IsNumber(v) ? v->valueint : 100) / 100.0f, (cJSON_IsNumber(p) ? p->valueint : 100) / 100.0f);
			}
			cJSON_Delete(mj);
		}
	}

	float px = (playerTileX + 0.5f) * tileSize.x, py = (playerTileY + 0.5f) * tileSize.y;
	if (!keepPlayer) {
		player = new Player(this, state, 999999, px, py);
		keepPlayer.reset(player);
	} else {
		player->MapChanged(999999, px, py);
	}
	player->SetDirection(facing);
	player->ReloadTextures();
	camFocusEntity = player;
	AddEntity(keepPlayer.release());   // AddEntity takes ownership

	for (auto &pr : parallelEventRunners)
		if (pr.first < 10000) pr.second->KeepAlive = false;
	SpawnFollowers();
	for (size_t i = 0; i < entities.size(); i++)
		if (entities[i]->eventTrigger == Entity::EventTrigger::ParallelProcess) entities[i]->Update();

	UpdateCamera();
	int nlayers = 0;
	for (auto &kv : heightsToLayers) nlayers += (int)kv.second.size();
	sys::log("map %d loaded: %dx%d, %d tilesets, %d layers, %d entities, %d lines (%.1f ms), heap %d MB used\n",
	         mapId, W, H, (int)tilesets.size(), nlayers, (int)entities.size(), (int)mapLines.size(),
	         (sys::time_us() - t0) / 1000.0, sys::heap_used_kb() / 1024);
	auto ms = [](uint64_t a, uint64_t b) { return (b - a) / 1000.0; };
	uint64_t tEnd = sys::time_us();
	sys::log("  load times (ms): read %.1f, xml %.1f, tilesets %.1f, panorama %.1f, layers %.1f, events %.1f, entities %.1f, rest %.1f\n",
	         ms(t0, tRead), ms(tRead, tXml), ms(tXml, tTilesets), ms(tTilesets, tPanorama), ms(tPanorama, tLayers), ms(tLayers, tEvents),
	         ms(tEvents, tEntities), ms(tEntities, tEnd));
	return true;
}

bool TileMap::DebugTeleport(int mapId, float x, float y, Direction facing) {
	// Retired, not deleted: released at the start of the next EventRunnerUpdate.
	if (mainEventRunner) retiredRunners.push_back(std::move(mainEventRunner));
	for (auto &pr : parallelEventRunners) retiredRunners.push_back(std::move(pr.second));
	parallelEventRunners.clear();
	queuedCommonEventId = -1;
	changeMapTimer = totalChangeMapTime = 0;
	screenTone = startScreenTone = targetScreenTone = GameTone();
	screenToneShiftTimer = screenToneShiftTotalTime = 0;
	shakePower = shakeSpeed = shakeDuration = 0;
	shakeOffset = 0;
	camFocusEntity = player;
	loadError.clear();
	state->set_flag(11, false);   // "exit transition" (common event 9) in progress
	if (player) player->DebugResetPlayerState();
	sys::log("debug teleport: map %d -> %d at (%d,%d)\n", currentMapId, mapId, (int)x, (int)y);
	if (!LoadMap(mapId, x, y, facing)) return false;
	if (player->GetNPCSheet().empty()) player->SetNPCSheet("niko", 0);
	return true;
}

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

void TileMap::KeepEntityOnMap(Entity *e) {
	Vec2 pos = e->GetPos(), vel = e->GetVel();
	if (pos.x < 0) { pos.x = 0; vel.x = 0; }
	else if (pos.x > mapSize.x * tileSize.x * 256) { pos.x = mapSize.x * tileSize.x * 256; vel.x = 0; }
	if (pos.y < 0) { pos.y = 0; vel.y = 0; }
	else if (pos.y > mapSize.y * tileSize.y * 256) { pos.y = mapSize.y * tileSize.y * 256; vel.y = 0; }
	e->SetPos(pos);
	e->SetVel(vel);
}

// ---------------------------------------------------------------- hashing
Rect TileMap::GetEntityHashRect(Entity *e, Vec2 p) const {
	Rect c = e->GetCollisionRect();
	int a = ((p.x + c.x * 256) / 256 - hashTileSize.x / 2) / hashTileSize.x;
	int b = ((p.y + c.y * 256) / 256 - hashTileSize.y / 2) / hashTileSize.y;
	int cc = ((p.x + (c.x + c.w) * 256) / 256 + hashTileSize.x / 2) / hashTileSize.x;
	int d = ((p.y + (c.y + c.h) * 256) / 256 + hashTileSize.y / 2) / hashTileSize.y;
	a = std::max(a, 0); b = std::max(b, 0); cc = std::max(cc, 0); d = std::max(d, 0);
	if (a >= hashMapSize.x) a = hashMapSize.x;
	if (b >= hashMapSize.y) b = hashMapSize.y;
	if (cc >= hashMapSize.x) cc = hashMapSize.x;
	if (d >= hashMapSize.y) d = hashMapSize.y;
	return Rect(a, b, cc - a, d - b);
}

void TileMap::RemoveEntityFromHash(Entity *e) {
	Rect r = GetEntityHashRect(e, e->GetOldPos());
	for (int i = r.x; i <= r.x + r.w; i++)
		for (int j = r.y; j <= r.y + r.h; j++) {
			auto it = hashedEntities.find(j * hashMapSize.x + i);
			if (it == hashedEntities.end()) continue;
			auto &v = it->second;
			auto f = std::find(v.begin(), v.end(), e);
			if (f != v.end()) v.erase(f);
		}
}

void TileMap::HashEntities() {
	for (auto &up : entities) {
		Entity *e = up.get();
		if (e->NeverHash || (!e->HasMoved() && e->HasBeenHashed)) continue;
		Rect o = GetEntityHashRect(e, e->GetOldPos()), r = GetEntityHashRect(e, e->GetPos());
		if (o == r && e->HasBeenHashed) continue;
		RemoveEntityFromHash(e);
		for (int i = r.x; i <= r.x + r.w; i++)
			for (int j = r.y; j <= r.y + r.h; j++) {
				auto &v = hashedEntities[j * hashMapSize.x + i];
				if (std::find(v.begin(), v.end(), e) == v.end()) v.push_back(e);
			}
		e->HasBeenHashed = true;
	}
}

void TileMap::HashedEntityCollision(Entity *e, Entity *ignored, bool isTest) {
	Vec2 pos = e->GetPos();
	int key = (pos.y / 256 / hashTileSize.y) * hashMapSize.x + (pos.x / 256 / hashTileSize.x);
	struct EWD { Entity *e; int d; };
	EWD buf[64];
	int n = 0;
	auto it = hashedEntities.find(key);
	if (it != hashedEntities.end())
		for (Entity *o : it->second)
			if (o != e && o != ignored && !o->IgnoreNpcCollision() && o->Active() && n < 64) {
				Vec2 p2 = o->GetPos();
				buf[n++] = { o, iabs(pos.x - p2.x) + iabs(pos.y - p2.y) };
			}
	std::stable_sort(buf, buf + n, [](const EWD &a, const EWD &b) { return a.d < b.d; });
	for (int i = 0; i < n; i++) {
		if (buf[i].e->IsPlayer() && e->GetVel().x == 0 && e->GetVel().y == 0 && !isTest)
			buf[i].e->EntityCollision(e);
		else
			e->EntityCollision(buf[i].e);
	}
}

// ---------------------------------------------------------------- collision
void TileMap::MapCollision(Entity *entity) {
	Vec2 ep = entity->GetPos();
	Rect cr = entity->GetCollisionRect();
	Vec2 a((ep.x / 256 + cr.x) / tileSize.x, (ep.y / 256 + cr.y) / tileSize.y);
	Vec2 b((ep.x / 256 + cr.x + cr.w) / tileSize.x, (ep.y / 256 + cr.y + cr.h) / tileSize.y);
	auto clampv = [&](Vec2 &v) {
		if (v.x < 0) v.x = 0; else if (v.x >= mapSize.x) v.x = mapSize.x - 1;
		if (v.y < 0) v.y = 0; else if (v.y >= mapSize.y) v.y = mapSize.y - 1;
	};
	clampv(a);
	clampv(b);
	Vec2 tiles[32];
	int nt = 0;
	MapLine *lines[64];
	int nl = 0;
	for (int i = a.x; i <= b.x; i++)
		for (int j = a.y; j <= b.y; j++) {
			auto hl = hashedMapLines.find(j * mapSize.x + i);
			if (hl != hashedMapLines.end())
				for (MapLine *ml : hl->second) {
					bool dup = false;
					for (int k = 0; k < nl; k++) if (lines[k]->id == ml->id) { dup = true; break; }
					if (!dup && nl < 64) lines[nl++] = ml;
				}
			if (IsTileSolid(i, j) && nt < 32)
				tiles[nt++] = Vec2(i * tileSize.x * 256 + tileSize.x * 256 / 2, j * tileSize.y * 256 + tileSize.y * 256 / 2);
		}
	std::stable_sort(tiles, tiles + nt, [&](const Vec2 &t1, const Vec2 &t2) {
		return iabs(ep.x - t1.x) + iabs(ep.y - t1.y) < iabs(ep.x - t2.x) + iabs(ep.y - t2.y);
	});
	for (int k = 0; k < nt; k++) {
		Rect tc(tiles[k].x - tileSize.x * 256 / 2, tiles[k].y - tileSize.y * 256 / 2, tileSize.x * 256, tileSize.y * 256);
		TileCollision(tc, entity, tc.x / (tileSize.x * 256), tc.y / (tileSize.y * 256), true);
	}
	// C# iterates a Dictionary<int, MapLine> by insertion order.
	for (int k = 0; k < nl; k++) lines[k]->Collision(entity);
}

void TileMap::TileCollision(Rect tc, Entity *entity, int tileX, int tileY, bool isMapTile) {
	Vec2 pos = entity->GetPos();
	Rect cr = entity->GetCollisionRect();
	Rect rect(pos.x + cr.x * 256, pos.y + cr.y * 256, cr.w * 256, cr.h * 256);
	if (rect.x >= tc.x + tc.w || rect.x + rect.w <= tc.x || rect.y >= tc.y + tc.h || rect.y + rect.h <= tc.y) return;
	Vec2 v(tc.x + tc.w / 2, tc.y + tc.h / 2);
	Vec2 v2(rect.x + rect.w / 2, rect.y + rect.h / 2);
	Vec2 vel = entity->GetVel();
	Direction dir = Direction::None;
	int n = iabs(v2.x - v.x), n2 = iabs(v2.y - v.y);
	if (tc.w > tc.h) n2 += (tc.w - tc.h) / 2; else n += (tc.h - tc.w) / 2;
	const int n3 = 0;
	if (n - n2 > n3) {
		dir = (v2.x >= v.x) ? Direction::Left : Direction::Right;
		if (vel.y == 0) {
			if (v2.y < tc.y) pos.y -= iabs(vel.x / 2);
			else if (v2.y > tc.y + tc.h) pos.y += iabs(vel.x / 2);
		}
	} else if (n - n2 < -n3) {
		dir = (v2.y >= v.y) ? Direction::Up : Direction::Down;
		if (vel.x == 0) {
			if (v2.x < tc.x) pos.x -= iabs(vel.y / 2);
			else if (v2.x > tc.x + tc.w) pos.x += iabs(vel.y / 2);
		}
	} else if (v2.x < v.x) {
		if (v2.y < v.y) {
			bool f = false;
			if (vel.x > 0 && vel.x > vel.y) {
				if (isMapTile && tileY > 0) f = IsTileSolid(tileX, tileY - 1);
				dir = f ? Direction::Right : Direction::Down;
			} else {
				if (isMapTile && tileX > 0) f = IsTileSolid(tileX - 1, tileY);
				dir = f ? Direction::Down : Direction::Right;
			}
		} else {
			bool f = false;
			if (vel.x > 0 && vel.x > -vel.y) {
				if (isMapTile && tileY < mapSize.y - 1) f = IsTileSolid(tileX, tileY + 1);
				dir = f ? Direction::Right : Direction::Up;
			} else {
				if (isMapTile && tileX > 0) f = IsTileSolid(tileX - 1, tileY);
				dir = f ? Direction::Up : Direction::Right;
			}
		}
	} else if (v2.y < v.y) {
		bool f = false;
		if (vel.x < 0 && vel.x < -vel.y) {
			if (isMapTile && tileY > 0) f = IsTileSolid(tileX, tileY - 1);
			dir = f ? Direction::Left : Direction::Down;
		} else {
			if (isMapTile && tileX < mapSize.x - 1) f = IsTileSolid(tileX + 1, tileY);
			dir = f ? Direction::Down : Direction::Left;
		}
	} else {
		bool f = false;
		if (vel.x < 0 && vel.x < vel.y) {
			if (isMapTile && tileY < mapSize.y - 1) f = IsTileSolid(tileX, tileY + 1);
			dir = f ? Direction::Left : Direction::Up;
		} else {
			if (isMapTile && tileX < mapSize.x - 1) f = IsTileSolid(tileX + 1, tileY);
			dir = f ? Direction::Up : Direction::Left;
		}
	}
	switch (dir) {
	case Direction::Right: pos.x = tc.x - rect.w - cr.x * 256; break;
	case Direction::Left:  pos.x = tc.x + tc.w - cr.x * 256; break;
	case Direction::Down:  pos.y = tc.y - rect.h - cr.y * 256; break;
	case Direction::Up:    pos.y = tc.y + tc.h - cr.y * 256; break;
	default: break;
	}
	entity->SetCollisionDirection(dir);
	entity->SetPos(pos);
}

// ---------------------------------------------------------------- update / camera
void TileMap::Update() {
	wasMapChangedDuringUpdate = false;
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

void TileMap::MovePlayerRelative(int x, int y) {
	Vec2 p = player->GetPos();
	p.x += x * tileSize.x * 256;
	p.y += y * tileSize.y * 256;
	player->SetPos(p);
	UpdateCamera();
}

float TileMap::GetPlayerXTile() const { return (float)player->GetPixelPos().x / (float)tileSize.x - 0.5f; }
float TileMap::GetPlayerYTile() const { return (float)player->GetPixelPos().y / (float)tileSize.y - 0.5f; }

void TileMap::PixelPuzzleReset() {
	for (auto &e : entities)
		if (e->GetName().compare(0, 6, "pixel ") == 0) state->set_self_switch(e->GetID(), "A", false);
}

bool TileMap::PixelPuzzleCheck(const uint8_t *solution, Vec2 start, Vec2 size) const {
	for (auto &e : entities) {
		if (e->GetName().compare(0, 6, "pixel ") != 0) continue;
		Vec2 t = e->GetCurrentTile();
		Vec2 v(t.x - start.x, t.y - start.y);
		if (v.x >= 0 && v.x < size.x && v.y >= 0 && v.y < size.y)
			if ((solution[v.x + v.y * size.x] != 0) != state->self_switch(e->GetID(), "A")) return false;
	}
	return true;
}

bool TileMap::RamPuzzleCheck() const {
	static const Vec2 ok[5] = { Vec2(80, 59), Vec2(79, 63), Vec2(80, 65), Vec2(83, 59), Vec2(85, 61) };
	for (auto &e : entities) {
		if (e->GetName().compare(0, 8, "sokoram ") != 0) continue;
		Vec2 t = e->GetCurrentTile();
		bool found = false;
		for (auto &p : ok) if (p == t) { found = true; break; }
		if (!found) return false;
	}
	return true;
}

bool TileMap::AreAnyEntitiesInForcedRoutes() const {
	if (player && player->IsInForcedRoute()) return true;
	for (auto &e : entities)
		if (e->IsInForcedRoute()) return true;
	return false;
}

void TileMap::SpawnFollowers() {
	static bool warned;
	if (!state->followers.empty() && !warned) {
		warned = true;
		sys::log("followers in party but follower entities are not ported yet\n");
	}
}

// ---------------------------------------------------------------- events
bool TileMap::CheckIfStartScript(Entity *e, bool investigate, Entity **out) {
	Entity *found = nullptr;
	Vec2 pos = e->GetPos();
	Rect cr = e->GetCollisionRect();
	Rect bp = e->GetButtonPressRect();
	Rect pointRect(pos.x - 1, pos.y - 1, 2, 2);
	Vec2 bpCenter(bp.x + bp.w / 2, bp.y + bp.h / 2);
	Rect bodyRect(pos.x + cr.x * 256, pos.y + cr.y * 256, cr.w * 256, cr.h * 256);
	// C# collects candidates into a Dictionary keyed by id (insertion order).
	std::vector<Entity *> cand;
	Rect hr = GetEntityHashRect(e, pos);
	for (int i = hr.x; i <= hr.x + hr.w; i++)
		for (int j = hr.y; j <= hr.y + hr.h; j++) {
			auto it = hashedEntities.find(j * hashMapSize.x + i);
			if (it == hashedEntities.end()) continue;
			for (Entity *o : it->second) {
				bool dup = false;
				for (Entity *c2 : cand) if (c2->GetID() == o->GetID()) { dup = true; break; }
				if (!dup) cand.push_back(o);
			}
		}
	int best = INT32_MAX;
	for (Entity *o : cand) {
		bool hit = false;
		if (o->Active() && o->HasScript()) {
			if (investigate && o->StartScriptOnPlayerAction()) {
				if (o->TouchingRect(bp)) hit = true;
				if (o->IgnoreNpcCollision() && o->TouchingRect(pointRect)) hit = true;
			}
			if (o->StartScriptOnPlayerTouch()) {
				if (!o->IgnoreNpcCollision() && o->TouchingRect(bp) && !o->isTouchingPlayerAndTriggeredEvent) hit = true;
				if (o->IgnoreNpcCollision() && o->TouchingRect(pointRect) && !o->isTouchingPlayerAndTriggeredEvent) hit = true;
			}
			if (o->StartScriptOnEventTouch() && o->TouchingRect(bodyRect) && !o->isTouchingPlayerAndTriggeredEvent) hit = true;
		}
		if (hit) {
			Vec2 p2 = o->GetPos();
			int d = iabs(p2.x - bpCenter.x) + iabs(p2.y - bpCenter.y);
			if (!found || d < best) { found = o; best = d; }
		}
	}
	if (found && (found->StartScriptOnPlayerTouch() || found->StartScriptOnEventTouch()))
		found->isTouchingPlayerAndTriggeredEvent = true;
	*out = found;
	return found != nullptr;
}

void TileMap::StartEvent(Entity *e, bool parallel) {
	if (parallel) {
		for (auto &pr : parallelEventRunners)
			if (pr.first == e->GetID()) { pr.second->KeepAlive = true; return; }
		parallelEventRunners.emplace_back(e->GetID(),
			std::unique_ptr<EventRunner>(new EventRunner(this, e->GetList(), e->GetID(), e->GetCurrentPageNumber(), e)));
		parallelEventRunners.back().second->KeepAlive = true;
		return;
	}
	if (mainEventRunner || IsMapTransitioning()) return;
	mainEventRunner.reset(new EventRunner(this, e->GetList(), e->GetID(), e->GetCurrentPageNumber(), e));
	mainEventRunner->Update();
	if (!mainEventRunner || !mainEventRunner->IsFinished()) return;
	mainEventRunner.reset();
	if (state->var(4) == 0) return;
	for (size_t i = 0; i < entities.size(); i++)
		if (entities[i]->HandlesMapEventVar) { entities[i]->Update(); break; }
}

void TileMap::StartCommonEvent(int id, bool parallel) {
	auto it = commonEvents.find(id);
	if (it == commonEvents.end()) {
		sys::log("No common event exists with id %d\n", id);
		return;
	}
	if (parallel) {
		int key = 10000 + id;
		for (auto &pr : parallelEventRunners)
			if (pr.first == key) { pr.second->KeepAlive = true; return; }
		parallelEventRunners.emplace_back(key, std::unique_ptr<EventRunner>(new EventRunner(this, it->second.list, id, -1)));
		parallelEventRunners.back().second->KeepAlive = true;
	} else if (!mainEventRunner) {
		mainEventRunner.reset(new EventRunner(this, it->second.list, id, -1));
		queuedCommonEventId = -1;
	} else {
		queuedCommonEventId = id;
	}
}

// Host testing: run a list of Show Text commands as a script.
void TileMap::DebugRunTexts(const std::vector<std::string> &texts) {
	auto list = std::make_shared<CommandList>();
	for (const std::string &t : texts) {
		EventCommand c;
		c.code = 101;
		c.parameters.push_back(t);
		list->push_back(c);
	}
	list->push_back(EventCommand());
	mainEventRunner.reset(new EventRunner(this, list, 0, -1));
}

const CommonEvent *TileMap::GetCommonEvent(int id) const {
	auto it = commonEvents.find(id);
	if (it == commonEvents.end()) {
		sys::log("No common event exists with id %d\n", id);
		return nullptr;
	}
	return &it->second;
}

Entity *TileMap::GetNPCTriggeringEvent() const {
	return mainEventRunner ? mainEventRunner->TriggeringEntity() : nullptr;
}

void TileMap::UpdateCommonEvents() {
	for (auto &kv : commonEvents) {
		const CommonEvent &ce = kv.second;
		if (ce.trigger == 1) {
			if (!IsInScript() && state->flag(ce.switch_id)) StartCommonEvent(ce.id);
		} else if (ce.trigger == 2) {
			if (state->flag(ce.switch_id)) StartCommonEvent(ce.id, true);
		}
	}
}

void TileMap::EventRunnerUpdate() {
	retiredRunners.clear();
	if (mainEventRunner) {
		mainEventRunner->Update();
		if (mainEventRunner->IsFinished()) mainEventRunner.reset();
	} else if (queuedCommonEventId > 0) {
		StartCommonEvent(queuedCommonEventId);
	}
	std::vector<int> kill;
	// Index loop: a runner may start new parallel events (e.g. after a transfer).
	for (size_t i = 0; i < parallelEventRunners.size(); i++) {
		EventRunner *r = parallelEventRunners[i].second.get();
		if (r->KeepAlive) {
			r->Update();
			r->KeepAlive = false;
			if (r->IsFinished()) kill.push_back(parallelEventRunners[i].first);
		} else {
			kill.push_back(parallelEventRunners[i].first);
		}
	}
	for (int k : kill)
		for (size_t i = 0; i < parallelEventRunners.size(); i++)
			if (parallelEventRunners[i].first == k) { parallelEventRunners.erase(parallelEventRunners.begin() + i); break; }
	ReleaseRetired();
}

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
#include "cJSON.h"

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
		else if (!kv.second->IsPlayer()) gone.push_back(kv.second);
	}
	for (Entity *g : gone) RemoveEntity(g);
	const cJSON *sheet = cJSON_GetObjectItemCaseSensitive(o, "playerSheet");
	player->SetNPCSheet(cJSON_IsString(sheet) ? sheet->valuestring : "");
	const cJSON *tone = cJSON_GetObjectItemCaseSensitive(o, "mapAmbientTone");
	if (tone) ambientTone = GameTone(jn(tone, "r", 0), jn(tone, "g", 0), jn(tone, "b", 0));
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
