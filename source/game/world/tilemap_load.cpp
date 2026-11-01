// tilemap_load.cpp - TileMapManager's map loading: the tileset data, a
// map's .tmx and events, the collision/priority layers, the entities, and
// reading ahead what its events will show.
#include "tilemap_internal.h"
#include "event_runner.h"
#include "player.h"
#include "special_entities.h"
#include "../ui/message_box.h"
#include "../../engine/audio.h"
#include "../../engine/gm.h"
#include "../../engine/xml.h"
#include "../../platform/sys.h"
#include "../core/game_state.h"
#include "../core/loc.h"
#include "cJSON.h"
#include <algorithm>
#include <ctype.h>
#include <functional>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ======================================================================= TileMap: loading

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


bool TileMap::LoadMap(int mapId, float playerTileX, float playerTileY, Direction facing) {
	uint64_t t0 = sys::time_us();
	bool hadMap = currentMapId != 0;
	loc::load_map(mapId);   // LanguageManager.LoadMapLocFile: this map's translated text
	heightsToLayers.clear();
	tilesets.clear();
	entitiesByHeight.clear();
	hashedEntities.clear();
	entitiesById.clear();
	mapLines.clear();
	hashedMapLines.clear();
	fireflies.clear();
	hasFireflies = false;
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
		for (auto &ed : eventFile->events) {
			if (ed.name == "!collectible") AddEntity(new Collectible(this, state, &ed));
			else if (ed.name == "@text" || ed.name == "@text_small") AddEntity(new CreditsText(this, state, &ed, ed.name == "@text_small"));
			else AddEntity(new Entity(this, state, &ed));
		}

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
	PrefetchEventAssets();
	return true;
}

// Reads ahead, on the loader thread, the faces, sprites and pictures this
// map's events (and the common events they call) will show, so a text box
// or a graphic change doesn't stall a frame waiting on the hard drive.
void TileMap::PrefetchEventAssets() {
	if (!eventFile) return;
	uint64_t t0 = sys::time_us();
	std::string sheet = player ? player->GetNPCSheet() : "";
	bool f160 = state->flag(160);
	std::vector<std::string> names;
	auto add = [&](const std::string &n) {
		if (!n.empty() && std::find(names.begin(), names.end(), n) == names.end()) names.push_back(n);
	};
	auto sprite = [&](const std::string &n) {
		if (n.empty()) return;
		add("npc/" + n);
		add("lightmaps/" + n);
	};
	auto faces = [&](const std::string &text) {
		for (size_t i = text.find('@'); i != std::string::npos; i = text.find('@', i + 1)) {
			size_t e = i + 1;
			while (e < text.size() && (isalnum((unsigned char)text[e]) || text[e] == '_')) e++;
			std::string tok = text.substr(i + 1, e - i - 1);
			bool glyph = !tok.empty();   // button glyphs (@OK, @MOVE...) are upper case
			for (char ch : tok) glyph &= !islower((unsigned char)ch);
			if (!tok.empty() && !glyph) add("facepics/" + portrait_name(tok, sheet, f160));
		}
	};
	auto route = [&](const MoveRoutePtr &r) {
		if (!r) return;
		for (auto &m : r->list)
			if (m.code == 41 && !m.parameters.empty()) sprite(m.parameters[0]);   // Change Graphic
	};
	std::vector<int> commons;
	std::function<void(const CommandListPtr &)> scan = [&](const CommandListPtr &l) {
		if (!l) return;
		for (auto &c : *l) {
			switch (c.code) {
			case 101: case 401: faces(c.param(0)); break;                  // Show Text
			case 231: if (!c.param(1).empty()) add("pictures/" + c.param(1)); break;   // Show Picture
			case 322: sprite(c.param(1)); break;                         // Change Actor Graphic
			case 209: route(c.move_route); break;                        // Set Move Route
			case 117: {                                                   // Call Common Event
				int id = c.iparam(0);
				if (std::find(commons.begin(), commons.end(), id) == commons.end()) {
					commons.push_back(id);
					if (const CommonEvent *ce = GetCommonEvent(id)) scan(ce->list);
				}
				break;
			}
			default: break;
			}
		}
	};
	for (auto &e : eventFile->events)
		for (auto &p : e.pages) {
			sprite(p.graphic.character_name);
			route(p.move_route);
			scan(p.list);
		}
	for (auto &n : names) gm::prefetch(n.c_str());
	sys::log("map %d: prefetching up to %d textures (%.1f ms to list)\n", GetMapID(), (int)names.size(), (sys::time_us() - t0) / 1000.0);
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
