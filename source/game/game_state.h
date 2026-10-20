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
