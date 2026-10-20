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
	std::vector<int> items;          // ItemManager inventory, in pickup order
	std::vector<int> followers;      // FollowerManager party (FollowerType ids)
	std::string player_name = "Player";

	bool has_item(int id) const { for (int i : items) if (i == id) return true; return false; }
	void add_item(int id) { if (!has_item(id)) items.push_back(id); }
	void remove_item(int id) { for (size_t i = 0; i < items.size(); i++) if (items[i] == id) { items.erase(items.begin() + i); return; } }
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
