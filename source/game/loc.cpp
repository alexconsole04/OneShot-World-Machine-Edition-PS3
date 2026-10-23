#include "loc.h"
#include "../engine/util.h"
#include "../engine/font.h"
#include "../engine/gm.h"
#include "../platform/sys.h"
#include "json_file.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <map>
#include <set>
#include <unordered_map>

// cJSON is strict; the game's JSON has trailing commas.
cJSON *parse_json_file(const char *rel) {
	uint8_t *d;
	size_t n;
	if (!sys::read_file(rel, &d, &n)) return nullptr;
	std::string s((const char *)d, n), out;
	free(d);
	out.reserve(s.size());
	bool str = false;
	for (size_t i = 0; i < s.size(); i++) {
		char c = s[i];
		if (str) {
			out += c;
			if (c == '\\' && i + 1 < s.size()) out += s[++i];
			else if (c == '"') str = false;
			continue;
		}
		if (c == '"') str = true;
		if (c == ',') {
			size_t j = i + 1;
			while (j < s.size() && (s[j] == ' ' || s[j] == '\t' || s[j] == '\r' || s[j] == '\n')) j++;
			if (j < s.size() && (s[j] == '}' || s[j] == ']')) continue;
		}
		out += c;
	}
	return cJSON_Parse(out.c_str());
}


namespace loc {

typedef std::unordered_map<std::string, std::string> Table;

static std::vector<Language> g_langs;
static Language g_cur;
static bool g_setOnStartup = true;
static int g_generation;

static Table twmStrs, commonEventStrs, itemStrs, mapNameStrs, zoneStrs, musicStrs, cgNameStrs;
static Table builtin;   // the port's own strings (English), under the .po ones
static std::set<int> mapFiles;            // maps the language has an eventstrs file for
static std::map<int, Table> mapCache;     // recently loaded map files
static std::vector<int> mapCacheOrder;
static int curMap = -1, prevMap = -1;      // ids in mapCache, or -1 (no file)
static std::map<std::string, std::set<std::string>> texDirs;   // "<dir>" -> localized names in <dir>/<code>/

// ------------------------------------------------------------------ .po reading (SimplePOReader)

// A JSON string literal starting at p (on the opening quote).
static std::string json_unquote(const char *p) {
	std::string o;
	if (*p != '"') return o;
	for (p++; *p && *p != '"'; p++) {
		if (*p != '\\' || !p[1]) { o += *p; continue; }
		p++;
		switch (*p) {
		case 'n': o += '\n'; break;
		case 't': o += '\t'; break;
		case 'r': o += '\r'; break;
		case 'b': o += '\b'; break;
		case 'f': o += '\f'; break;
		case 'u': {
			unsigned v = 0;
			int k = 0;
			for (; k < 4 && p[1]; k++) {
				char c = *++p;
				v = v * 16 + (c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : 0);
			}
			if (v < 0x80) o += (char)v;
			else if (v < 0x800) { o += (char)(0xC0 | (v >> 6)); o += (char)(0x80 | (v & 63)); }
			else { o += (char)(0xE0 | (v >> 12)); o += (char)(0x80 | ((v >> 6) & 63)); o += (char)(0x80 | (v & 63)); }
			break;
		}
		default: o += *p; break;   // \" \\ \/
		}
	}
	return o;
}

static bool read_po(const std::string &rel, Table &out, bool parseKeys) {
	out.clear();
	uint8_t *data;
	size_t size;
	if (!sys::file_exists(rel.c_str()) || !sys::read_file(rel.c_str(), &data, &size)) return false;
	std::vector<std::string> lines;
	{
		const char *s = (const char *)data, *e = s + size;
		while (s < e) {
			const char *nl = (const char *)memchr(s, '\n', e - s);
			const char *le = nl ? nl : e;
			const char *te = le;
			if (te > s && te[-1] == '\r') te--;
			lines.emplace_back(s, te);
			s = nl ? nl + 1 : e;
		}
	}
	free(data);
	std::string key;
	for (size_t i = 0; i < lines.size(); i++) {
		const std::string &l = lines[i];
		if (l.empty() || l[0] == '#') continue;
		bool id = l.compare(0, 6, "msgid ") == 0, str = l.compare(0, 7, "msgstr ") == 0;
		if (!id && !str) continue;
		std::string v = json_unquote(l.c_str() + (id ? 6 : 7));
		while (i + 1 < lines.size() && !lines[i + 1].empty() && lines[i + 1][0] == '"') v += json_unquote(lines[++i].c_str());
		if (id) {
			if (parseKeys) {
				size_t eq = v.find('=');
				if (eq != std::string::npos) key = v.substr(0, eq);
			} else if (!v.empty()) {
				key = v;
			}
		} else if (!key.empty()) {
			out[key] = v;
			key.clear();
		}
	}
	return true;
}

// ------------------------------------------------------------------ metadata

static void set_builtin() {
	// Strings the console build adds or this port adds (English), and
	// fallbacks if a language's file is missing.
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
		{ "settings_language", "Language" },
		{ "oneshot_window_no_close_during_cutscene", "You cannot perform this action during cutscenes." },
		{ "game_load_failed", "Save data is corrupt. Deleting save." },
		{ "boot_lang_select", "Select your Language" }, { "boot_lang_select_hint", "(Press @OK to confirm)" },
		{ "boot_autosave_explain1", "This game has an autosave feature." },
		{ "boot_autosave_explain2", "When you see this icon on screen, please do not close the game." },
	};
	for (auto &d : defaults) builtin[d[0]] = d[1];
}

void init() {
	set_builtin();
	g_langs.clear();
	if (cJSON *j = parse_json_file("gamedata/loc/language_metadata.json")) {
		const cJSON *it;
		cJSON_ArrayForEach(it, cJSON_GetObjectItemCaseSensitive(j, "languages")) {
			auto str = [&](const char *k) {
				const cJSON *v = cJSON_GetObjectItemCaseSensitive(it, k);
				return std::string(cJSON_IsString(v) ? v->valuestring : "");
			};
			Language l;
			l.code = str("lang_code");
			l.displayName = str("display_name");
			l.fontOS = str("font_os");
			l.fontGame = str("font_game");
			l.fontGameSmall = str("font_game_small");
			const cJSON *sc = cJSON_GetObjectItemCaseSensitive(it, "font_os_scale");
			l.fontOSScale = cJSON_IsString(sc) ? atoi(sc->valuestring) : cJSON_IsNumber(sc) ? sc->valueint : 2;
			if (!l.code.empty()) g_langs.push_back(l);
		}
		cJSON_Delete(j);
	}
	if (g_langs.empty()) {
		sys::log("loc: gamedata/loc/language_metadata.json missing or unreadable, English only\n");
		Language en;
		en.code = "en";
		en.displayName = "English";
		en.fontOS = "volter";
		en.fontGame = "terminus";
		en.fontGameSmall = "terminus_doc";
		en.fontOSScale = 2;
		g_langs.push_back(en);
	}
	// LoadSavedLanguageCode: language.dat, else the console's language (the
	// boot sequence then asks, with that one picked).
	std::string saved;
	std::string codeToUse = sys::system_language();
	bool valid = false;
	if (sys::save_read("language.dat", saved)) {
		while (!saved.empty() && (saved.back() == '\n' || saved.back() == '\r' || saved.back() == ' ')) saved.pop_back();
		for (auto &l : g_langs)
			if (l.code == saved) valid = true;
	}
	if (valid) codeToUse = saved;
	else g_setOnStartup = false;
	bool haveEn = false;
	for (auto &l : g_langs)
		if (l.code == codeToUse) haveEn = true;
	if (!haveEn) codeToUse = g_langs[0].code;
	set(codeToUse, false);
	sys::log("loc: %d languages, using %s%s\n", (int)g_langs.size(), g_cur.code.c_str(),
	         g_setOnStartup ? "" : " (first launch: the boot sequence will ask)");
}

const std::vector<Language> &languages() { return g_langs; }
const Language &current() { return g_cur; }
const std::string &code() { return g_cur.code; }
bool was_set_on_startup() { return g_setOnStartup; }
int generation() { return g_generation; }

void set(const std::string &c, bool save) {
	const Language *l = nullptr;
	for (auto &x : g_langs)
		if (x.code == c) l = &x;
	if (!l) {
		sys::log("loc: tried to set language code that's not in the metadata: %s\n", c.c_str());
		return;
	}
	uint64_t t0 = sys::time_us();
	g_cur = *l;
	std::string base = "gamedata/loc/" + g_cur.code;
	read_po(base + "/common_event_strs.po", commonEventStrs, false);
	read_po(base + "/item_strs.po", itemStrs, true);
	read_po(base + "/map_name_strs.po", mapNameStrs, true);
	read_po(base + "/map_zone_name_strs.po", zoneStrs, true);
	read_po(base + "/twm/cg_name_strs.po", cgNameStrs, true);
	read_po(base + "/music/music_strs.po", musicStrs, true);
	read_po(base + "/twm/twm_strs.po", twmStrs, true);
	mapFiles.clear();
	{
		std::vector<std::string> names;
		sys::list_dir((base + "/maps").c_str(), names);
		for (auto &n : names) {
			std::string ln = util::lower(n);
			if (ln.compare(0, 13, "eventstrs_map") == 0) mapFiles.insert(atoi(ln.c_str() + 13));
		}
	}
	mapCache.clear();
	mapCacheOrder.clear();
	curMap = prevMap = -1;
	texDirs.clear();
	font::set_language_fonts(g_cur.fontOS.c_str(), g_cur.fontGame.c_str(), g_cur.fontGameSmall.c_str(), g_cur.fontOSScale);
	gm::set_texture_redirect(texture_name);
	if (g_generation > 0)   // textures may have localized versions: load them again
		for (int c = 0; c < gm::CACHE_COUNT; c++)
			if (c != gm::Glyphs) gm::clear_cache((gm::CacheType)c);
	g_generation++;
	sys::log("loc: language %s (%s): %d menu, %d common event, %d item strings, %d map files (%.0f ms)\n", g_cur.code.c_str(),
	         g_cur.displayName.c_str(), (int)twmStrs.size(), (int)commonEventStrs.size(), (int)itemStrs.size(), (int)mapFiles.size(),
	         (sys::time_us() - t0) / 1000.0);
	if (save && !sys::save_write("language.dat", g_cur.code)) sys::log("loc: couldn't write language.dat\n");
	if (save) g_setOnStartup = true;
}

// ------------------------------------------------------------------ lookups

static std::string get(const Table &t, const std::string &key, const std::string &original) {
	auto it = t.find(key);
	return it != t.end() ? it->second : original;
}

const std::string &twm(const char *key) {
	auto it = twmStrs.find(key);
	if (it != twmStrs.end()) return it->second;
	auto b = builtin.find(key);
	if (b != builtin.end()) return b->second;
	return builtin.emplace(key, key).first->second;
}

void load_map(int mapId) {
	prevMap = curMap;
	curMap = -1;
	if (!mapFiles.count(mapId)) return;
	if (!mapCache.count(mapId)) {
		Table t;
		char rel[96];
		snprintf(rel, sizeof(rel), "gamedata/loc/%s/maps/eventstrs_map%d.po", g_cur.code.c_str(), mapId);
		read_po(rel, t, false);
		mapCache.emplace(mapId, std::move(t));
		mapCacheOrder.push_back(mapId);
		// Keep the files of the last few maps (walking back and forth).
		for (size_t i = 0; mapCacheOrder.size() > 6 && i < mapCacheOrder.size();) {
			int old = mapCacheOrder[i];
			if (old == mapId || old == prevMap) { i++; continue; }
			mapCache.erase(old);
			mapCacheOrder.erase(mapCacheOrder.begin() + i);
		}
	}
	curMap = mapId;
}

static const Table *map_table(int id) {
	if (id < 0) return nullptr;
	auto it = mapCache.find(id);
	return it == mapCache.end() ? nullptr : &it->second;
}

std::string map_text(int pageId, const std::string &original) {
	if (pageId < 0) return get(commonEventStrs, original, original);
	if (const Table *t = map_table(curMap)) {
		auto it = t->find(original);
		if (it != t->end()) return it->second;
	}
	if (const Table *t = map_table(prevMap)) {
		auto it = t->find(original);
		if (it != t->end()) return it->second;
	}
	return original;
}

std::string item_name(int id, const std::string &o) { return get(itemStrs, std::to_string(id) + ":name", o); }
std::string item_description(int id, const std::string &o) { return get(itemStrs, std::to_string(id) + ":description", o); }
std::string map_name(int mapId, const std::string &o) { return get(mapNameStrs, std::to_string(mapId), o); }
std::string zone(const std::string &z, const std::string &o) { return get(zoneStrs, z, o); }
std::string music(const std::string &t, const std::string &o) { return get(musicStrs, t, o); }
std::string cg_name(const std::string &i, const std::string &o) { return get(cgNameStrs, i, o); }

std::string localized_path(const char *rel) {
	std::string p = "gamedata/loc/" + g_cur.code + "/" + rel;
	if (sys::file_exists(p.c_str())) return p;
	return std::string("gamedata/loc/en/") + rel;
}

std::string texture_name(const std::string &name) {
	if (g_cur.code == "en") return name;   // there are no English-specific textures
	size_t slash = name.find_last_of('/');
	std::string dir = slash == std::string::npos ? "" : name.substr(0, slash);
	std::string file = slash == std::string::npos ? name : name.substr(slash + 1);
	auto it = texDirs.find(dir);
	if (it == texDirs.end()) {
		// One directory listing per texture folder, instead of a file check
		// per texture (each costs a hard drive access on the PS3).
		std::set<std::string> s;
		std::vector<std::string> names;
		std::string rel = "content/" + (dir.empty() ? "" : dir + "/") + g_cur.code;
		sys::list_dir(rel.c_str(), names);
		for (auto &n : names) {
			std::string ln = util::lower(n);
			if (ln.size() > 4 && ln.compare(ln.size() - 4, 4, ".xnb") == 0) s.insert(ln.substr(0, ln.size() - 4));
		}
		it = texDirs.emplace(dir, std::move(s)).first;
	}
	if (!it->second.count(util::lower(file))) return name;
	return (dir.empty() ? "" : dir + "/") + g_cur.code + "/" + file;
}

} // namespace loc
