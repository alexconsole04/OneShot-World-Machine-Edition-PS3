#include "loc.h"
#include "../platform/sys.h"
#include <stdlib.h>
#include <string.h>
#include <unordered_map>

namespace loc {

static std::unordered_map<std::string, std::string> strs;
static bool loaded;

// "..." with C escapes, starting at p (after the opening quote).
static std::string unquote(const char *p) {
	std::string o;
	for (; *p && *p != '"'; p++) {
		if (*p == '\\' && p[1]) {
			p++;
			o += *p == 'n' ? '\n' : *p == 't' ? '\t' : *p;
		} else {
			o += *p;
		}
	}
	return o;
}

static void load() {
	loaded = true;
	// Strings the console build adds, and fallbacks if the file is missing.
	static const char *defaults[][2] = {
		{ "mainmenu_fasttravel", "Fast Travel" }, { "mainmenu_notes", "Notes" }, { "mainmenu_settings", "Settings" },
		{ "mainmenu_quit", "Quit" }, { "title_option_start", "Start" }, { "title_option_settings", "Settings" },
		{ "title_option_exit", "Exit" }, { "title_option_dots", "..." }, { "title_help_text_1", "Move with @MOVE" },
		{ "title_help_text_2", "Select with @OK" }, { "settings_title", "Settings" }, { "settings_movement", "Default Movement" },
		{ "settings_colorblind", "Colorblind Mode" }, { "settings_vibration", "Controller Vibration" },
		{ "settings_screentear", "Screen Tear Effects" }, { "settings_chromaaberration", "Chromatic Aberration Effects" },
		{ "settings_automash", "Skip Text (@MASHTEXT)" }, { "settings_on", "ON" },
		{ "settings_ps3_scaling", "Scaling" }, { "settings_ps3_scaling_sharp", "SHARP" },
		{ "settings_ps3_scaling_smooth", "SMOOTH" }, { "settings_ps3_scaling_integer", "INTEGER" },
		{ "settings_ps3_scaling_direct", "DIRECT" }, { "settings_off", "OFF" },
		{ "settings_run", "Run" }, { "settings_walk", "Walk" },
		{ "oneshot_window_no_close_during_cutscene", "You cannot perform this action during cutscenes." },
		{ "game_load_failed", "Save data is corrupt. Deleting save." },
	};
	for (auto &d : defaults) strs[d[0]] = d[1];
	uint8_t *data;
	size_t size;
	if (!sys::read_file("gamedata/loc/en/twm/twm_strs.po", &data, &size)) {
		sys::log("loc: gamedata/loc/en/twm/twm_strs.po missing, using built-in English\n");
		return;
	}
	std::string key;
	int n = 0;
	char *text = (char *)data;
	for (char *line = strtok(text, "\r\n"); line; line = strtok(nullptr, "\r\n")) {
		if (strncmp(line, "msgid \"", 7) == 0) {
			std::string id = unquote(line + 7);
			size_t eq = id.find('=');
			key = id.substr(0, eq);
		} else if (strncmp(line, "msgstr \"", 8) == 0 && !key.empty()) {
			std::string v = unquote(line + 8);
			if (!v.empty()) { strs[key] = v; n++; }
			key.clear();
		}
	}
	free(data);
	sys::log("loc: %d strings\n", n);
}

const std::string &twm(const char *key) {
	if (!loaded) load();
	auto it = strs.find(key);
	if (it != strs.end()) return it->second;
	auto r = strs.emplace(key, key);
	return r.first->second;
}

} // namespace loc
