// game_state.h - FlagManager / VariableManager / SelfSwitchManager equivalents.
#pragma once
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
	bool travel_enabled = true;
	std::string journal_page;
	int plight_timer_start = 0;          // minutes, local clock
	bool window_maximized = false;       // "$console" / Graphics.fullscreen
	long long play_frames = 0;           // GameSaveManager.PlayTimeFrameCount
	long long bruteforce_start = -1;     // PlightBruteForceStartFrame (-1 = unset)

	bool has_item(int id) const { return id >= 0 && id < (int)inventory.size() && inventory[id]; }
	void add_item(int id) { if (id >= 0 && id < (int)inventory.size()) inventory[id] = true; }
	void remove_item(int id) { if (id >= 0 && id < (int)inventory.size()) inventory[id] = false; }
	bool has_follower(int f) const { for (int i : followers) if (i == f) return true; return false; }

	bool flag(int id) const { return id >= 0 && id < (int)flags.size() && flags[id]; }
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
