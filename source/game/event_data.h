// event_data.h - Event / Page / Graphic / Condition (OneShotMG.src.Entities.Event)
// loaded from gamedata/maps/events_mapN.json. Command lists are kept as
// parsed JSON nodes for the event interpreter (next milestone).
#pragma once
#include <string>
#include <vector>
#include <memory>

struct cJSON;

struct MoveCommand {
	int code = 0;
	std::vector<std::string> parameters;
};

struct MoveRoute {
	bool repeat = false, skippable = false;
	std::vector<MoveCommand> list;
};

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
	MoveRoute move_route;
	bool walk_anime = false, step_anime = false, direction_fix = false, through = false;
	bool always_on_top = false, always_on_bottom = false;
	int trigger = 0;
	cJSON *list = nullptr;   // event commands (owned by the EventFile)
};

struct EventData {
	int id = 0;
	std::string name;
	int x = 0, y = 0;
	std::vector<EventPage> pages;
};

// Owns the parsed JSON so command lists stay valid for the map's lifetime.
struct EventFile {
	std::vector<EventData> events;
	cJSON *root = nullptr;
	~EventFile();
};

std::unique_ptr<EventFile> load_event_file(const char *rel_path);
