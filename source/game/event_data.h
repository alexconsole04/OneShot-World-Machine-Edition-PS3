// event_data.h - Event / Page / Graphic / Condition / EventCommand / MoveRoute
// (OneShotMG.src.Entities) loaded from gamedata/maps/events_mapN.json and
// gamedata/oneshot_common_events.json.
//
// Command lists and move routes are shared_ptr-owned: an EventRunner keeps
// the list it is executing alive across a map change (in C# the garbage
// collector did this), and entities keep forced move routes alive the same way.
#pragma once
#include <string>
#include <vector>
#include <memory>

struct AudioFile {
	std::string name;
	int volume = 100, pitch = 100;
	bool valid = false;
};

struct MoveCommand {
	int code = 0;
	std::vector<std::string> parameters;
	AudioFile audio_file;
};

struct MoveRoute {
	bool repeat = false, skippable = false;
	std::vector<MoveCommand> list;
};
using MoveRoutePtr = std::shared_ptr<const MoveRoute>;

struct EventCommand {
	int code = 0;
	int indent = 0;
	std::vector<std::string> parameters;
	MoveRoutePtr move_route;
	AudioFile audio_file;
	const std::string &param(size_t i) const {
		static const std::string empty;
		return i < parameters.size() ? parameters[i] : empty;
	}
	int iparam(size_t i) const;   // int.Parse(parameters[i]); 0 if missing / malformed
	float fparam(size_t i) const;
};
using CommandList = std::vector<EventCommand>;
using CommandListPtr = std::shared_ptr<const CommandList>;

struct EventPage {
	struct Condition {
		bool switch1_valid = false, switch2_valid = false, variable_valid = false, self_switch_valid = false;
		int switch1_id = 1, switch2_id = 1, variable_id = 1, variable_value = 0;
		std::string self_switch_ch;
	} condition;
	struct Graphic {
		int tile_id = 0;
		std::string character_name;
		int character_hue = 0, direction = 2, pattern = 0, opacity = 255, blend_type = 0;
	} graphic;
	int move_type = 0, move_speed = 2, move_frequency = 2;
	MoveRoutePtr move_route = std::make_shared<MoveRoute>();
	bool walk_anime = false, step_anime = false, direction_fix = false, through = false;
	bool always_on_top = false, always_on_bottom = false;
	int trigger = 0;
	CommandListPtr list = std::make_shared<CommandList>();
};

struct EventData {
	int id = 0;
	std::string name;
	int x = 0, y = 0;
	std::vector<EventPage> pages;
};

struct EventFile {
	std::vector<EventData> events;
};

struct CommonEvent {
	int id = 0;
	std::string name;
	int trigger = 0, switch_id = 0;
	CommandListPtr list = std::make_shared<CommandList>();
};

std::unique_ptr<EventFile> load_event_file(const char *rel_path);
// Returns false if the file is missing or malformed (out is left empty).
bool load_common_events(const char *rel_path, std::vector<CommonEvent> &out);
