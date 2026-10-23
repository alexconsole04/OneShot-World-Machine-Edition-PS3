#include "debug_map_menu.h"
#include "oneshot_window.h"
#include "player.h"
#include "../engine/audio.h"
#include "../engine/gm.h"
#include "../engine/overlay.h"
#include "../platform/gfx.h"
#include "../platform/input.h"
#include "../platform/sys.h"
#include "../platform/thread.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <map>
#include <string>
#include <vector>

namespace debug_map_menu {

struct Entry { int x, y, dir, from; };
struct MapInfo {
	int id;
	std::string name;
	int group;                  // tab index (1..)
	std::vector<Entry> entries; // filled by the transfer scan
};
struct Tab { std::string name; unsigned rgb; std::vector<int> maps; };   // indexes into maps

static bool loaded, opened;
static int loadStage;             // 0 = not loaded, 1 = "loading" frame drawn, 2 = ready
static std::vector<MapInfo> maps;
static std::vector<Tab> tabs;
static int tab, sel[16], scroll[16], entrySel;
static int currentMap;
static std::string message;

// ---------------------------------------------------------------- data
// The game's JSON files have trailing commas, which cJSON rejects.
static cJSON *parse_lenient(const char *rel) {
	uint8_t *d;
	size_t n;
	if (!sys::read_file(rel, &d, &n)) return nullptr;
	std::string s((const char *)d, n);
	free(d);
	std::string out;
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

static MapInfo *find_map(int id) {
	for (auto &m : maps)
		if (m.id == id) return &m;
	return nullptr;
}

// Transfer Player (code 201) with a direct destination (parameter 0 == 0):
// [0, map, x, y, direction, fade]. Parameters may be strings or numbers.
// A plain text scan is many times faster than parsing 14 MB of JSON.
struct RawEntry { int target, x, y, dir, from; };

static void scan_transfers(const char *s, int from, std::vector<RawEntry> &out) {
	const char *p = s;
	while ((p = strstr(p, "\"code\"")) != nullptr) {
		p += 6;
		while (*p == ' ' || *p == ':') p++;
		if (strtol(p, nullptr, 10) != 201) continue;
		const char *q = strstr(p, "\"parameters\"");
		const char *next = strstr(p, "\"code\"");
		if (!q || (next && next < q)) continue;
		q = strchr(q, '[');
		if (!q) break;
		q++;
		int v[5] = { -1, 0, 0, 0, 0 };
		for (int i = 0; i < 5; i++) {
			while (*q == ' ' || *q == '"') q++;
			char *e;
			v[i] = (int)strtol(q, &e, 10);
			if (e == q) { v[0] = -1; break; }
			q = e;
			while (*q == ' ' || *q == '"') q++;
			if (*q != ',') { if (i < 4) v[0] = -1; break; }
			q++;
		}
		if (v[0] == 0) out.push_back({ v[1], v[2], v[3], v[4], from });
	}
}

static void apply_entries(const std::vector<RawEntry> &raw) {
	for (auto &m : maps) m.entries.clear();
	for (const RawEntry &r : raw) {
		MapInfo *m = find_map(r.target);
		if (!m || m->entries.size() >= 24) continue;
		bool dup = false;
		for (auto &e : m->entries)
			if (e.x == r.x && e.y == r.y) { dup = true; break; }
		if (!dup) m->entries.push_back({ r.x, r.y, r.dir, r.from });
	}
}

// Scanning all 263 event files takes ~5 s on the PS3's hard drive (about
// 30 ms to open each), so it runs on its own thread and the result is kept
// in the save folder; later sessions read that one file instead.
static const char *CACHE_FILE = "map_entries.txt";
static std::vector<std::string> scan_files;   // event files, set before the thread starts
static std::vector<RawEntry> scan_out;        // written only by the scan thread until scan_state == 2
static int scan_state;                        // 0 idle, 1 scanning, 2 finished (atomic)
static uint64_t scan_ms;
static bool entries_ready;

static void scan_all(void *) {
	uint64_t t0 = sys::time_us();
	for (auto &f : scan_files) {
		int id;
		if (sscanf(f.c_str(), "events_map%d.json", &id) != 1) continue;
		uint8_t *d;
		size_t n;
		std::string rel = "gamedata/maps/" + f;
		if (!sys::read_file(rel.c_str(), &d, &n)) continue;
		scan_transfers((const char *)d, id, scan_out);
		free(d);
	}
	uint8_t *d;
	size_t n;
	if (sys::read_file("gamedata/oneshot_common_events.json", &d, &n)) {
		scan_transfers((const char *)d, 0, scan_out);
		free(d);
	}
	scan_ms = (sys::time_us() - t0) / 1000;
	__atomic_store_n(&scan_state, 2, __ATOMIC_RELEASE);
}

static bool load_cache(int fileCount) {
	std::string text;
	if (!sys::save_read(CACHE_FILE, text)) return false;
	int ver = 0, count = -1, pos = 0;
	if (sscanf(text.c_str(), "v%d %d\n%n", &ver, &count, &pos) < 2 || ver != 1 || count != fileCount) return false;
	std::vector<RawEntry> raw;
	const char *p = text.c_str() + pos;
	RawEntry r;
	int used;
	while (sscanf(p, "%d %d %d %d %d\n%n", &r.target, &r.x, &r.y, &r.dir, &r.from, &used) == 5) {
		raw.push_back(r);
		p += used;
	}
	apply_entries(raw);
	return true;
}

static void save_cache(int fileCount, const std::vector<RawEntry> &raw) {
	std::string out = "v1 " + std::to_string(fileCount) + "\n";
	char line[64];
	for (const RawEntry &r : raw) {
		snprintf(line, sizeof(line), "%d %d %d %d %d\n", r.target, r.x, r.y, r.dir, r.from);
		out += line;
	}
	sys::save_write(CACHE_FILE, out);
}

static int event_file_count;

static void poll_scan() {
	if (entries_ready || __atomic_load_n(&scan_state, __ATOMIC_ACQUIRE) != 2) return;
	apply_entries(scan_out);
	save_cache(event_file_count, scan_out);
	int withEntries = 0;
	for (auto &m : maps) withEntries += !m.entries.empty();
	sys::log("map switcher: entry points found in the background (%d event files, %d maps with entries, %u ms); saved\n",
	         event_file_count, withEntries, (unsigned)scan_ms);
	scan_out.clear();
	scan_out.shrink_to_fit();
	entries_ready = true;
}

static void load() {
	uint64_t t0 = sys::time_us();
	maps.clear();
	tabs.clear();
	std::vector<std::string> files;
	sys::list_dir("gamedata/maps", files);
	for (auto &f : files) {
		int id;
		char tail[8];
		if (sscanf(f.c_str(), "map%d.%4s", &id, tail) == 2 && strcmp(tail, "tmx") == 0) maps.push_back({ id, "", 0, {} });
	}
	std::sort(maps.begin(), maps.end(), [](const MapInfo &a, const MapInfo &b) { return a.id < b.id; });

	if (cJSON *n = parse_lenient("gamedata/oneshot_map_names.json")) {
		const cJSON *it;
		cJSON_ArrayForEach(it, cJSON_GetObjectItemCaseSensitive(n, "map_names")) {
			const cJSON *id = cJSON_GetObjectItemCaseSensitive(it, "id"), *nm = cJSON_GetObjectItemCaseSensitive(it, "name");
			if (MapInfo *m = cJSON_IsNumber(id) ? find_map(id->valueint) : nullptr)
				if (cJSON_IsString(nm)) m->name = nm->valuestring;
		}
		cJSON_Delete(n);
	}
	std::map<std::string, std::string> zone;
	if (cJSON *z = parse_lenient("gamedata/oneshot_map_zone_names.json")) {
		const cJSON *it;
		cJSON_ArrayForEach(it, z)
			if (cJSON_IsString(it) && it->string) zone[it->string] = it->valuestring;
		cJSON_Delete(z);
	}
	tabs.push_back({ "All maps", 0xffffff, {} });
	if (cJSON *c = parse_lenient("gamedata/oneshot_map_colors.json")) {
		const cJSON *grp;
		cJSON_ArrayForEach(grp, cJSON_GetObjectItemCaseSensitive(c, "mapColors")) {
			const cJSON *nm = cJSON_GetObjectItemCaseSensitive(grp, "name");
			std::string key = cJSON_IsString(nm) ? nm->valuestring : "?";
			std::string label = key == "purple" ? "House / Tower"
			                  : key == "blue" ? (zone.count("Blue") ? zone["Blue"] : "The Barrens")
			                  : key == "green" ? (zone.count("Green") ? zone["Green"] : "The Glen")
			                  : key == "red" ? (zone.count("Red") ? zone["Red"] : "The Refuge") : key;
			const cJSON *col = cJSON_GetObjectItemCaseSensitive(grp, "color");
			auto ch = [&](const char *k) {
				const cJSON *v = cJSON_GetObjectItemCaseSensitive(col, k);
				int x = cJSON_IsNumber(v) ? v->valueint : 200;
				return (unsigned)(x + (255 - x) * 2 / 5);   // lighter, for dark backgrounds
			};
			unsigned rgb = (ch("r") << 16) | (ch("g") << 8) | ch("b");
			int g = (int)tabs.size();
			tabs.push_back({ label, rgb, {} });
			const cJSON *id;
			cJSON_ArrayForEach(id, cJSON_GetObjectItemCaseSensitive(grp, "maps"))
				if (MapInfo *m = cJSON_IsNumber(id) ? find_map(id->valueint) : nullptr) m->group = g;
		}
		cJSON_Delete(c);
	}
	int other = (int)tabs.size();
	tabs.push_back({ "Other", 0xb0b0b0, {} });
	if (tabs.size() > 16) tabs.resize(16);
	for (size_t i = 0; i < maps.size(); i++) {
		if (maps[i].group <= 0 || maps[i].group >= (int)tabs.size()) maps[i].group = other;
		tabs[0].maps.push_back((int)i);
		tabs[maps[i].group].maps.push_back((int)i);
	}

	uint64_t t1 = sys::time_us();
	scan_files.clear();
	for (auto &f : files) {
		int id;
		if (sscanf(f.c_str(), "events_map%d.json", &id) == 1) scan_files.push_back(f);
	}
	event_file_count = (int)scan_files.size();
	if (load_cache(event_file_count)) {
		entries_ready = true;
		sys::log("map switcher: %d maps, entry points from %s (%.0f ms)\n", (int)maps.size(), CACHE_FILE,
		         (sys::time_us() - t0) / 1000.0);
	} else {
		scan_state = 1;
		if (!sys::thread_start(scan_all, nullptr, "map scan", -1)) scan_all(nullptr);   // no thread: scan now
		sys::log("map switcher: %d maps listed in %.0f ms; finding entry points in the background\n", (int)maps.size(),
		         (t1 - t0) / 1000.0);
	}
	loaded = true;
}

// ---------------------------------------------------------------- UI
static int line_h() { return gfx::screen_h() >= 1000 ? 30 : 24; }
static float text_scale() { return gfx::screen_h() >= 1000 ? 2.0f : 1.5f; }
static int list_top() { return 40 + line_h() * 4; }
static int visible_rows() { return std::max(4, (gfx::screen_h() - list_top() - line_h() * 4) / line_h()); }

static MapInfo *selected_map() {
	if (tabs.empty()) return nullptr;
	Tab &t = tabs[tab];
	if (t.maps.empty()) return nullptr;
	return &maps[t.maps[std::min(sel[tab], (int)t.maps.size() - 1)]];
}

static void select_current_map() {
	for (int ti = 0; ti < (int)tabs.size(); ti++)
		for (int i = 0; i < (int)tabs[ti].maps.size(); i++)
			if (maps[tabs[ti].maps[i]].id == currentMap) {
				sel[ti] = i;
				if (ti != 0 && (tab == 0 || tab >= (int)tabs.size())) tab = ti;
			}
}

static void keep_visible() {
	int rows = visible_rows();
	int &s = sel[tab], &sc = scroll[tab];
	if (s < sc) sc = s;
	if (s >= sc + rows) sc = s - rows + 1;
	if (sc < 0) sc = 0;
}

void open(int cur) {
	if (opened) return;
	opened = true;
	currentMap = cur;
	entrySel = 0;
	message.clear();
	loadStage = loaded ? 2 : 0;
	tab = 0;
	if (loaded) { select_current_map(); keep_visible(); }
}

bool is_open() { return opened; }

void update() {
	if (!opened) return;
	if (loadStage == 0) { loadStage = 1; return; }   // draw "loading" for one frame first
	if (loadStage == 1) {
		load();
		loadStage = 2;
		tab = 0;
		select_current_map();
		keep_visible();
		return;
	}
	poll_scan();
	using namespace input;
	if (pressed(BTN_CANCEL) || pressed(BTN_SELECT)) { opened = false; return; }
	int nt = (int)tabs.size();
	if (nt == 0) return;
	int n = (int)tabs[tab].maps.size();
	int old = sel[tab];
	auto rep = [](uint32_t b, int interval) { return pressed(b) || auto_trigger(b, 18, interval); };
	if (rep(BTN_DOWN, 3)) sel[tab]++;
	if (rep(BTN_UP, 3)) sel[tab]--;
	if (rep(BTN_R1, 6)) sel[tab] += visible_rows();
	if (rep(BTN_L1, 6)) sel[tab] -= visible_rows();
	if (n > 0) {
		if (pressed(BTN_DOWN) && old == n - 1) sel[tab] = 0;   // wrap on a fresh press only
		else if (pressed(BTN_UP) && old == 0) sel[tab] = n - 1;
		sel[tab] = std::max(0, std::min(sel[tab], n - 1));
	} else {
		sel[tab] = 0;
	}
	if (pressed(BTN_RIGHT)) tab = (tab + 1) % nt;
	if (pressed(BTN_LEFT)) tab = (tab + nt - 1) % nt;
	if (sel[tab] != old || pressed(BTN_RIGHT) || pressed(BTN_LEFT)) entrySel = 0;
	keep_visible();
	MapInfo *m = selected_map();
	if (!m) return;
	int ne = (int)m->entries.size() + 1;   // + "map centre"
	if (pressed(BTN_RUN)) entrySel = (entrySel + 1) % ne;          // Square
	if (pressed(BTN_INVENTORY)) entrySel = (entrySel + ne - 1) % ne;   // Triangle
	if (pressed(BTN_OK)) {
		int x = -1, y = -1;
		Direction d = Direction::Down;
		if (entrySel < (int)m->entries.size()) {
			const Entry &e = m->entries[entrySel];
			x = e.x;
			y = e.y;
			if (e.dir == 2 || e.dir == 4 || e.dir == 6 || e.dir == 8) d = (Direction)e.dir;
		}
		bool ok = window().DebugTeleport(m->id, x < 0 ? 0 : x, y < 0 ? 0 : y, d);
		if (ok && x < 0) {
			TileMap &map = window().Map();
			Vec2 ms = map.GetMapSize();
			map.GetPlayer()->SetPosTile(map.FindOpenTileNear(ms.x / 2, ms.y / 2));
		}
		if (ok) window().Map().Update();   // settle the camera before the first draw
		opened = false;
	}
}

void draw() {
	if (!opened) return;
	int W = gfx::screen_w(), H = gfx::screen_h();
	gfx::Shade bg;
	bg.r = bg.g = bg.b = 0.04f;
	bg.a = 0.92f;
	gm::blit_screen(gm::white(), 0, 0, (float)W, (float)H, Rect(0, 0, 1, 1), bg);
	float s = text_scale();
	int lh = line_h();
	float x0 = W * 0.08f;
	if (loadStage < 2) {
		overlay::text(x0, 40, s, 0xffffff, "Debug map switcher - reading the map list...");
		return;
	}
	overlay::text(x0, 40, s, 0xffffff, "DEBUG MAP SWITCHER     (current map %d)", currentMap);
	// Tabs
	float tx = x0;
	for (int i = 0; i < (int)tabs.size(); i++) {
		char label[64];
		snprintf(label, sizeof(label), i == tab ? "[ %s (%d) ]" : "  %s (%d)  ", tabs[i].name.c_str(), (int)tabs[i].maps.size());
		float w = overlay::text_width(label) * s + 12 * s;
		if (tx + w > W - x0) { tx = x0; }
		overlay::text(tx, 40 + lh * 2, s, i == tab ? tabs[i].rgb : (tabs[i].rgb & 0xfefefe) >> 1, "%s", label);
		tx += w;
	}
	Tab &t = tabs[tab];
	int rows = visible_rows(), top = list_top();
	if (t.maps.empty()) overlay::text(x0, (float)top, s, 0xa0a0a0, "(no maps)");
	for (int r = 0; r < rows; r++) {
		int i = scroll[tab] + r;
		if (i >= (int)t.maps.size()) break;
		const MapInfo &m = maps[t.maps[i]];
		bool on = i == sel[tab];
		float y = (float)(top + r * lh);
		if (on) {
			gfx::Shade hl;
			hl.r = hl.g = hl.b = 0.25f;
			hl.a = 1;
			gm::blit_screen(gm::white(), x0 - 8, y - 4, W - 2 * x0 + 16, (float)lh, Rect(0, 0, 1, 1), hl);
		}
		unsigned col = on ? 0xffffff : tabs[m.group].rgb;
		overlay::text_plain(x0, y, s, col, (std::string(on ? "> " : "  ") + std::to_string(m.id)).c_str());
		std::string nm = m.name.empty() ? "(unnamed)" : m.name;
		if (tab == 0) nm += "   - " + tabs[m.group].name;
		if (m.id == currentMap) nm += "   <- you are here";
		overlay::text_plain(x0 + 70 * s, y, s, col, nm.c_str());
		char e[32];
		if (entries_ready) snprintf(e, sizeof(e), "%d entr%s", (int)m.entries.size(), m.entries.size() == 1 ? "y" : "ies");
		else snprintf(e, sizeof(e), "...");
		overlay::text_plain(W - x0 - overlay::text_width(e) * s, y, s, m.entries.empty() ? 0x808080 : 0xa0d0a0, e);
	}
	if (t.maps.size() > (size_t)rows)
		overlay::text(W - x0 - 160 * s, 40 + lh * 3.0f, s, 0x808080, "%d-%d of %d", scroll[tab] + 1,
		              std::min((int)t.maps.size(), scroll[tab] + rows), (int)t.maps.size());
	float fy = (float)(top + rows * lh + lh / 2);
	if (const MapInfo *m = selected_map()) {
		if (entrySel < (int)m->entries.size()) {
			const Entry &e = m->entries[entrySel];
			overlay::text(x0, fy, s, 0xffe0a0, "Arrive at: entry %d/%d  tile %d,%d  (a Transfer Player in %s %d)", entrySel + 1,
			              (int)m->entries.size(), e.x, e.y, e.from ? "map" : "common events", e.from ? e.from : 0);
		} else {
			overlay::text(x0, fy, s, 0xffe0a0, "Arrive at: an open tile near the middle of the map%s",
			              !entries_ready ? "  (still finding entry points in the background...)"
			              : m->entries.empty() ? "  (no Transfer Player leads here)" : "");
		}
	}
	overlay::text(x0, fy + lh * 1.2f, s, 0xa0a0a0,
	              "Up/Down: map   L1/R1: page   Left/Right: area   Square/Triangle: entry point   Cross: go   Circle: close");
	overlay::text(x0, fy + lh * 2.2f, s, 0x808080, "Going to a map stops any running cutscene. Story flags are not changed.");
}

} // namespace debug_map_menu
