// test_hooks.cpp - see test_hooks.h.
#include "test_hooks.h"
#include "game/core/game_state.h"
#include "game/core/oneshot_window.h"
#include "game/world/tilemap.h"
#include "platform/gfx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <vector>

namespace test_hooks {

// Splits "a<sep>b<sep>c" into its parts (empty input gives no parts).
static std::vector<std::string> split(const char *s, char sep) {
	std::vector<std::string> parts;
	if (!s || !*s) return parts;
	std::string all(s);
	size_t p = 0;
	while (true) {
		size_t e = all.find(sep, p);
		parts.push_back(all.substr(p, e == std::string::npos ? std::string::npos : e - p));
		if (e == std::string::npos) break;
		p = e + 1;
	}
	return parts;
}

uint32_t seed(uint32_t fallback) {
	const char *e = getenv("OSPS3_SEED");
	return e ? (uint32_t)strtoul(e, nullptr, 0) : fallback;
}

int start_map() {
	const char *m = getenv("OSPS3_MAP");
	return m ? atoi(m) : -1;
}

void start_position(float &x, float &y) {
	if (const char *s = getenv("OSPS3_SPAWN")) sscanf(s, "%f,%f", &x, &y);
}

bool skip_boot() { return start_map() >= 0 || getenv("OSPS3_SKIP_BOOT") != nullptr; }
bool launch() { return getenv("OSPS3_LAUNCH") != nullptr; }

void apply_game_state(GameState &state) {
	for (auto &f : split(getenv("OSPS3_FLAGS"), ','))   // "178,160"
		state.set_flag(atoi(f.c_str()), true);
	for (auto &v : split(getenv("OSPS3_VARS"), ',')) {   // "3=2,4=1"
		int id, value;
		if (sscanf(v.c_str(), "%d=%d", &id, &value) == 2) state.set_var(id, value);
	}
	for (auto &i : split(getenv("OSPS3_ITEMS"), ','))   // "3,4"
		state.add_item(atoi(i.c_str()));
	for (auto &t : split(getenv("OSPS3_TRAVEL"), ',')) {   // "zone:map:x:y,..." unlocked fast travel
		GameState::TravelLocation l;
		if (sscanf(t.c_str(), "%d:%d:%d:%d", &l.zone, &l.mapId, &l.x, &l.y) != 4) continue;
		state.travel_locations.push_back(l);
		state.travel_enabled = true;
		state.travel_zone = l.zone;
	}
	for (auto &f : split(getenv("OSPS3_FOLLOWERS"), ','))   // "2,3" (FollowerType)
		state.followers.push_back(atoi(f.c_str()));
}

void run_texts() {
	std::vector<std::string> texts = split(getenv("OSPS3_TEXTS"), '|');
	if (!texts.empty()) window().Map().DebugRunTexts(texts);
}

void screenshots(int frame) {
	struct Shot { int frame; std::string file; };
	static std::vector<Shot> shots;
	static bool parsed;
	if (!parsed) {
		parsed = true;
		for (auto &item : split(getenv("OSPS3_SHOTS"), ',')) {
			size_t colon = item.find(':');
			if (colon != std::string::npos) shots.push_back({ atoi(item.c_str()), item.substr(colon + 1) });
		}
	}
	for (auto &s : shots)
		if (s.frame == frame) gfx::screenshot(s.file.c_str());
}

bool audio_log() { return getenv("OSPS3_AUDIO_LOG") != nullptr; }
bool overlay() { return getenv("OSPS3_OVERLAY") != nullptr; }

bool texture_viewer(int &index) {
	const char *t = getenv("OSPS3_TEXVIEW");
	index = t ? atoi(t) : 0;
	return t != nullptr;
}

} // namespace test_hooks
