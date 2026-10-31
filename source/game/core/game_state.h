// game_state.h - FlagManager / VariableManager / SelfSwitchManager equivalents.
#pragma once
#include "settings.h"
#include "../twm/wm.h"
#include <string>
#include <unordered_map>
#include <vector>

struct GameState {
	std::vector<bool> flags = std::vector<bool>(1001, false);
	std::vector<int> vars = std::vector<int>(1001, 0);
	// key: mapId * 100000 + eventId, then per-letter bitmask A..D
	std::unordered_map<long long, int> self_switches;
	int map_id = 0;
	std::vector<bool> inventory = std::vector<bool>(81, false);   // ItemManager.currentInventory
	std::vector<int> followers;      // FollowerManager party (FollowerType ids)
	std::string player_name = "Player";
	// FastTravelManager
	struct TravelLocation { int zone = 0, mapId = 0; int x = 0, y = 0; int direction = 2; };
	std::vector<TravelLocation> travel_locations;
	bool travel_enabled = false;     // FastTravelManager.Unlocked
	int travel_zone = 0;             // FastTravelManager.CurrentZone (0 Blue, 1 Green, 2 Red, 3 RedGround)
	int plight_timer_start = 0;          // minutes, local clock
	long long play_frames = 0;           // GameSaveManager.PlayTimeFrameCount
	long long bruteforce_start = -1;     // PlightBruteForceStartFrame (-1 = unset)

	bool has_item(int id) const { return id >= 0 && id < (int)inventory.size() && inventory[id]; }
	void add_item(int id) { if (id >= 0 && id < (int)inventory.size()) inventory[id] = true; }
	void remove_item(int id) { if (id >= 0 && id < (int)inventory.size()) inventory[id] = false; }
	bool has_follower(int f) const { for (int i : followers) if (i == f) return true; return false; }

	// FlagManager.IsFlagSet, including its computed flags (401-469: the
	// desktop's unlocks).
	bool flag(int id) const {
		switch (id) {
		case 25: case 390: return true;
		case 395: case 396: return false;   // Steam demo builds
		case 252: return settings().colorblindMode;
		default: {
			if (id > 400) {
				int c = twm::computed_flag(id);
				if (c >= 0) return c != 0;
			}
			return id >= 0 && id < (int)flags.size() && flags[id];
		}
		}
	}
	bool solstice_glitch_time() const { return flag(353) && !flag(317); }   // IsSolticeGlitchTime
	void set_flag(int id, bool v) { if (id >= 0 && id < (int)flags.size()) flags[id] = v; }
	int var(int id) const { return id >= 0 && id < (int)vars.size() ? vars[id] : 0; }
	void set_var(int id, int v) { if (id >= 0 && id < (int)vars.size()) vars[id] = v; }

	bool self_switch(int event_id, const std::string &ch) const {
		if (ch.empty()) return false;
		auto it = self_switches.find((long long)map_id * 100000 + event_id);
		return it != self_switches.end() && (it->second & (1 << (ch[0] - 'A')));
	}
	void set_self_switch(int event_id, const std::string &ch, bool v) {
		if (ch.empty()) return;
		int &m = self_switches[(long long)map_id * 100000 + event_id];
		if (v) m |= 1 << (ch[0] - 'A'); else m &= ~(1 << (ch[0] - 'A'));
	}
};
