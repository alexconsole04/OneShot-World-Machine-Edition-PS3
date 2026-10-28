// fast_travel_menu.cpp - port of OneShotMG.src.Menus.FastTravelMenu and the
// data half of FastTravelManager (minimap pieces, edges, zone names).
//
// Not ported: during the solstice glitch the PC draws the minimap pieces
// through GlitchEffect; here they're drawn plainly (the chromatic
// aberration still runs, twice as strong, like the PC's).
#include "menus.h"
#include "game_state.h"
#include "json_file.h"
#include "loc.h"
#include "settings.h"
#include "tilemap.h"
#include "../engine/audio.h"
#include "../engine/font.h"
#include "../engine/gm.h"
#include "../engine/util.h"
#include "../platform/input.h"
#include "../platform/sys.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static const char *ZONE_KEYS[4] = { "Blue", "Green", "Red", "RedGround" };

static int zone_index(const char *k) {
	for (int i = 0; i < 4; i++)
		if (strcmp(k, ZONE_KEYS[i]) == 0) return i;
	return -1;
}

// Numbers in these files are sometimes strings ("MapId": "12").
static int jnum(const cJSON *o, const char *k, int def = 0) {
	const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
	if (cJSON_IsNumber(v)) return (int)v->valuedouble;
	if (cJSON_IsString(v)) return atoi(v->valuestring);
	return def;
}

static Vec2 jvec(const cJSON *o, const char *k) {
	const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
	return v ? Vec2(jnum(v, "X"), jnum(v, "Y")) : Vec2();
}

// MathHelper.EaseIn
static int ease_in(int start, int target, int t, int total) {
	float n = (float)t / (float)total;
	float k = 1.0f - (1.0f - n) * (1.0f - n) * (1.0f - n);
	return start + (int)roundf((float)(target - start) * k);
}

// MathHelper.HandleAbberateUpdate
static void aberrate(int &timer, Vec2 &offset, int strength) {
	if (timer > 0) {
		if (--timer <= 0) { timer = 0; offset = Vec2(); }
	} else if (util::random(1, 100) < 33) {
		timer = util::random(2, 8);
		offset = Vec2(util::random(-strength, strength), util::random(-strength, strength));
	}
}

void FastTravelMenu::Load() {
	if (loaded) return;
	loaded = true;
	zoneNames.assign(4, "");
	if (cJSON *z = parse_json_file("gamedata/oneshot_map_zone_names.json")) {
		const cJSON *it;
		cJSON_ArrayForEach(it, z) {
			int i = it->string ? zone_index(it->string) : -1;
			if (i >= 0 && cJSON_IsString(it)) zoneNames[i] = it->valuestring;
		}
		cJSON_Delete(z);
	}
	if (cJSON *j = parse_json_file("gamedata/oneshot_minimap_info.json")) {
		const cJSON *zn;
		cJSON_ArrayForEach(zn, cJSON_GetObjectItemCaseSensitive(j, "zones")) {
			int i = zn->string ? zone_index(zn->string) : -1;
			if (i < 0) continue;
			const cJSON *p;
			cJSON_ArrayForEach(p, zn) {
				Piece pc;
				pc.mapId = jnum(p, "MapId");
				pc.offset = jvec(p, "Offset");
				pc.warp = jvec(p, "WarpPoint");
				const cJSON *img = cJSON_GetObjectItemCaseSensitive(p, "Image");
				if (cJSON_IsString(img)) pc.image = img->valuestring;
				zones[i].push_back(pc);
			}
		}
		cJSON_Delete(j);
	} else {
		sys::log("fast travel: gamedata/oneshot_minimap_info.json missing\n");
	}
	if (cJSON *j = parse_json_file("gamedata/oneshot_minimap_nodes.json")) {
		const cJSON *zn;
		cJSON_ArrayForEach(zn, cJSON_GetObjectItemCaseSensitive(j, "zones")) {
			int i = zn->string ? zone_index(zn->string) : -1;
			if (i < 0) continue;
			const cJSON *m;
			cJSON_ArrayForEach(m, zn) {
				if (!m->string) continue;
				std::vector<Edge> &v = zoneEdges[i][atoi(m->string)];
				const cJSON *e;
				cJSON_ArrayForEach(e, m) {
					Edge ed;
					ed.id = jnum(e, "id");
					const cJSON *d = cJSON_GetObjectItemCaseSensitive(e, "direction");
					std::string ds = cJSON_IsString(d) ? d->valuestring : "up";
					ed.dir = ds == "down" ? 1 : ds == "left" ? 2 : ds == "right" ? 3 : 0;
					v.push_back(ed);
				}
			}
		}
		cJSON_Delete(j);
	}
	if (cJSON *n = parse_json_file("gamedata/oneshot_map_names.json")) {
		const cJSON *it;
		cJSON_ArrayForEach(it, cJSON_GetObjectItemCaseSensitive(n, "map_names")) {
			const cJSON *nm = cJSON_GetObjectItemCaseSensitive(it, "name");
			if (cJSON_IsString(nm)) mapNames[jnum(it, "id", -1)] = nm->valuestring;
		}
		cJSON_Delete(n);
	}
}

bool FastTravelMenu::IsOption(int mapId) const {
	for (int id : options)
		if (id == mapId) return true;
	return false;
}

// SetMinimapTarget
void FastTravelMenu::SetTarget(int mapId) {
	for (const Piece &p : *pieces) {
		if (p.mapId != mapId) continue;
		Vec2 t;
		if (p.HasWarp()) {
			t = p.warp;
		} else {
			Vec2 sz = gm::texture_size(("ui/minimap/" + p.image).c_str());
			t = Vec2(p.offset.x + sz.x / 2, p.offset.y + sz.y / 2);
		}
		cursorStart = cursor;
		cursorTarget = t;
		cursorMoveTimer = 20;
		shakeTimer = 4;
		break;
	}
	edges.clear();
	auto ze = zoneEdges.find(zone);
	if (ze == zoneEdges.end()) return;
	auto me = ze->second.find(current);
	if (me == ze->second.end()) return;
	for (const Edge &e : me->second)
		if (IsOption(e.id)) edges.push_back(e);
}

void FastTravelMenu::Open() {
	Load();
	audio::play_sound("menu_decision");
	st = Opening;
	opacity = 0;
	selected = -1;
	zone = state->travel_zone;
	options.clear();
	for (auto &l : state->travel_locations)
		if (l.zone == zone) options.push_back(l.mapId);
	static const std::vector<Piece> none;
	auto z = zones.find(zone);
	pieces = z != zones.end() ? &z->second : &none;
	current = map->GetMapID();
	if (!IsOption(current) && !options.empty()) current = options[0];
	edges.clear();
	if (!options.empty()) {
		SetTarget(current);
		cursor = cursorTarget;
		cursorMoveTimer = 0;
	}
}

void FastTravelMenu::Close() {
	st = Closing;
	opacity = 255;
}

void FastTravelMenu::Update() {
	switch (st) {
	case Opening:
		opacity += 20;
		if (opacity >= 255) { opacity = 255; st = Shown; }
		break;
	case Closing:
		opacity -= 20;
		if (opacity <= 0) {
			opacity = 0;
			if (selected >= 0 && selected != map->GetMapID()) {
				map->SetScreenTone(GameTone(-255, -255, -255), 30);   // GameTone.Black
				st = FadeOut;
				fadeTimer = 0;
			} else {
				st = Closed;
			}
		}
		break;
	case FadeOut:
		if (++fadeTimer >= 30) {
			for (auto &l : state->travel_locations)
				if (l.zone == zone && l.mapId == selected) {
					map->ChangeMap(l.mapId, (float)l.x, (float)l.y, 0.0f, (Direction)l.direction);
					break;
				}
			map->SetScreenTone(GameTone(), 30);
			st = FadeIn;
			fadeTimer = 0;
		}
		break;
	case FadeIn:
		if (++fadeTimer >= 30) { st = Closed; fadeTimer = 0; }
		break;
	case Shown: {
		static const uint32_t btn[4] = { input::BTN_UP, input::BTN_DOWN, input::BTN_LEFT, input::BTN_RIGHT };
		int id = current;
		for (int d = 0; d < 4 && id == current; d++) {
			if (!input::pressed(btn[d])) continue;
			for (const Edge &e : edges)
				if (e.dir == d) { id = e.id; break; }
			break;
		}
		if (id != current) {
			current = id;
			audio::play_sound("menu_cursor");
			SetTarget(current);
		}
		if (!options.empty() && input::pressed(input::BTN_OK)) {
			selected = current;
			audio::play_sound("menu_decision");
			Close();
		} else if (input::pressed(input::BTN_CANCEL)) {
			audio::play_sound("menu_cancel");
			Close();
		}
		break;
	}
	case Closed:
		break;
	}
	if (st == Closed) return;
	int strength = state->solstice_glitch_time() ? 2 : 1;
	aberrate(aberrTimer[0], aberr[0], strength);   // red
	aberrate(aberrTimer[2], aberr[2], strength);   // blue (the PC's order)
	aberrate(aberrTimer[1], aberr[1], strength);   // green
	if (cursorMoveTimer > 0) {
		cursorMoveTimer--;
		cursor = Vec2(ease_in(cursorStart.x, cursorTarget.x, 20 - cursorMoveTimer, 20),
		              ease_in(cursorStart.y, cursorTarget.y, 20 - cursorMoveTimer, 20));
	}
	if (shakeTimer > 0) {
		shakeTimer--;
		shake = Vec2(0, shakeTimer % 2 * 2 - 1);
	} else {
		shake = Vec2();
	}
	if (++arrowAnimTimer >= 40) arrowAnimTimer = 0;
}

void FastTravelMenu::DrawMinimap(float a) {
	bool chroma = settings().chromaAberration;
	// Unselected pieces are tinted with the zone's colour.
	float tr = 0.33f, tg = 0.3f, tb = 0.82f;
	if (zone == 1) { tr = 0.2f; tg = 0.66f; tb = 0.45f; }
	else if (zone >= 2) { tr = 0.84f; tg = 0.13f; tb = 0.4f; }
	for (int pass = chroma ? 0 : 1; pass < 2; pass++) {
		for (const Piece &p : *pieces) {
			if (!IsOption(p.mapId)) continue;
			const gfx::Texture *t = gm::texture(("ui/minimap/" + p.image).c_str());
			if (!t) continue;
			bool sel = !options.empty() && current == p.mapId;
			float r = sel ? 1 : tr, g = sel ? 1 : tg, b = sel ? 1 : tb;
			Rect src(0, 0, (int)t->w, (int)t->h);
			if (pass == 0) {
				gm::blit(t, p.offset + aberr[0], src, a / 2, 0, gfx::Blend::Additive, 2, GameTone(), r, 0, 0);
				gm::blit(t, p.offset + aberr[1], src, a / 2, 0, gfx::Blend::Additive, 2, GameTone(), 0, g, 0);
				gm::blit(t, p.offset + aberr[2], src, a / 2, 0, gfx::Blend::Additive, 2, GameTone(), 0, 0, b);
			} else {
				gm::blit(t, sel ? p.offset + shake : p.offset, src, a, 0, gfx::Blend::Normal, 2, GameTone(), r, g, b);
			}
		}
	}
	const Vec2 iconOff(-12, -12);
	if (const gfx::Texture *icon = gm::texture("ui/minimap/niko_icon")) {
		Rect src(0, 0, (int)icon->w, (int)icon->h);
		gm::blit(icon, iconOff + cursor, src, a);
		if (chroma) {
			gm::blit(icon, iconOff + cursor + aberr[0], src, a / 3, 0, gfx::Blend::Additive, 2, GameTone(), 1, 0, 0);
			gm::blit(icon, iconOff + cursor + aberr[1], src, a / 3, 0, gfx::Blend::Additive, 2, GameTone(), 0, 1, 0);
			gm::blit(icon, iconOff + cursor + aberr[2], src, a / 3, 0, gfx::Blend::Additive, 2, GameTone(), 0, 0, 1);
		}
	}
	const gfx::Texture *arrows = gm::texture("ui/minimap/small_arrows");
	if (!arrows) return;
	int bob = arrowAnimTimer / 20;
	for (const Edge &e : edges) {
		Rect src;
		Vec2 off;
		switch (e.dir) {
		default: src = Rect(0, 0, 16, 16); off = Vec2(-8, -24 - bob); break;
		case 1: src = Rect(16, 0, 16, 16); off = Vec2(-8, bob); break;
		case 2: src = Rect(32, 0, 16, 16); off = Vec2(-20 - bob, -12); break;
		case 3: src = Rect(48, 0, 16, 16); off = Vec2(4 + bob, -12); break;
		}
		gm::blit(arrows, off + cursor, src, a);
		if (chroma) {
			gm::blit(arrows, off + cursor + aberr[0], src, a / 2, 0, gfx::Blend::Additive, 2, GameTone(), 1, 0, 0);
			gm::blit(arrows, off + cursor + aberr[1], src, a / 2, 0, gfx::Blend::Additive, 2, GameTone(), 0, 1, 0);
			gm::blit(arrows, off + cursor + aberr[2], src, a / 2, 0, gfx::Blend::Additive, 2, GameTone(), 0, 0, 1);
		}
	}
}

void FastTravelMenu::Draw() {
	if (st == Closed || st == FadeOut || st == FadeIn || !pieces) return;
	float a = opacity / 255.0f;
	gm::color_box(Rect(0, 0, 320, 240), 0, 0, 0, (uint8_t)(opacity * 180 / 255));
	DrawMinimap(a);
	std::string zn = zone >= 0 && zone < 4 && !zoneNames[zone].empty() ? loc::zone(ZONE_KEYS[zone], zoneNames[zone])
	                                                                    : loc::twm("fasttravel_no_zone");
	font::draw(font::Game, Vec2(13, 30), zn, 0xffffff, a, 2);
	auto mn = mapNames.find(current);
	std::string name = mn != mapNames.end() ? loc::map_name(current, mn->second) : loc::twm("mapname_no_map_name");
	font::draw_centered(font::Game, Vec2(320, 420), name, 0xffffff, a, 1);
}
