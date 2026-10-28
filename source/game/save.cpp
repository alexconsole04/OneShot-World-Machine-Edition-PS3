#include "save.h"
#include "game_state.h"
#include "pictures.h"
#include "settings.h"
#include "twm/fs.h"
#include "twm/wm.h"
#include "desktop.h"
#include "tilemap.h"
#include "oneshot_window.h"
#include "../engine/audio.h"
#include "../platform/sys.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <algorithm>
#include <string>

namespace save {

static const char *SAVE_FILE = "save.dat";
static const char *PERMA_FILE = "p-settings.dat";
static const int SAVE_VERSION = 1;
static const int PERMA_FLAGS_START = 151, PERMA_VARS_START = 76, PERMA_COUNT = 25;

static int jint(const cJSON *o, const char *k, int def) {
	const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
	return cJSON_IsNumber(v) ? (int)v->valuedouble : def;
}
static double jdbl(const cJSON *o, const char *k, double def) {
	const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
	return cJSON_IsNumber(v) ? v->valuedouble : def;
}
static std::string jstr(const cJSON *o, const char *k) {
	const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
	return cJSON_IsString(v) ? v->valuestring : "";
}

static bool write_json(const char *name, cJSON *root) {
	char *s = cJSON_PrintUnformatted(root);
	cJSON_Delete(root);
	if (!s) return false;
	bool ok = sys::save_write(name, s);
	cJSON_free(s);
	return ok;
}

static cJSON *read_json(const char *name) {
	std::string text;
	if (!sys::save_read(name, text)) return nullptr;
	cJSON *root = cJSON_Parse(text.c_str());
	if (!root) sys::log("save: %s is damaged\n", name);
	return root;
}

static const char *FAKESAVE = "/docs_foldername/mygames_foldername/oneshot_foldername/fakesave_filename";

// SaveExists: the playthrough's file on the desktop is the save - delete
// it there and the save goes too.
bool Exists() {
	if (desktop::ready() && !twm::filesystem().FileExists(FAKESAVE)) Erase();
	return sys::save_exists(SAVE_FILE);
}

bool SavedFlag(int id) {
	cJSON *root = read_json(SAVE_FILE);
	if (!root) return false;
	bool r = false;
	const cJSON *it;
	cJSON_ArrayForEach(it, cJSON_GetObjectItemCaseSensitive(root, "flags"))
		if (cJSON_IsNumber(it) && (int)it->valuedouble == id) r = true;
	cJSON_Delete(root);
	return r;
}

// ---------------------------------------------------------------- permanent data
static void WritePerma(const GameState *s) {
	cJSON *root = cJSON_CreateObject();
	cJSON *f = cJSON_AddArrayToObject(root, "permaFlags");
	cJSON *v = cJSON_AddArrayToObject(root, "permaVars");
	for (int i = 0; i < PERMA_COUNT; i++) {
		cJSON_AddItemToArray(f, cJSON_CreateBool(s->flag(i + PERMA_FLAGS_START)));
		cJSON_AddItemToArray(v, cJSON_CreateNumber(s->var(i + PERMA_VARS_START)));
	}
	cJSON_AddStringToObject(root, "playerName", s->player_name.c_str());
	write_json(PERMA_FILE, root);
}

bool LoadPerma(GameState *s) {
	cJSON *root = read_json(PERMA_FILE);
	if (!root) return false;
	const cJSON *f = cJSON_GetObjectItemCaseSensitive(root, "permaFlags");
	const cJSON *v = cJSON_GetObjectItemCaseSensitive(root, "permaVars");
	if (!cJSON_IsArray(f) || !cJSON_IsArray(v)) {
		cJSON_Delete(root);
		return false;
	}
	for (int i = 0; i < PERMA_COUNT; i++) {
		const cJSON *fi = cJSON_GetArrayItem(f, i), *vi = cJSON_GetArrayItem(v, i);
		s->set_var(i + PERMA_VARS_START, cJSON_IsNumber(vi) ? (int)vi->valuedouble : 0);
		s->set_flag(i + PERMA_FLAGS_START, cJSON_IsTrue(fi));
	}
	std::string name = jstr(root, "playerName");
	if (!name.empty()) s->player_name = name;
	cJSON_Delete(root);
	return true;
}

void KillPermaFlags(GameState *s) {
	for (int i = 0; i < PERMA_COUNT; i++) {
		s->set_var(i + PERMA_VARS_START, 0);
		s->set_flag(i + PERMA_FLAGS_START, false);
	}
}

// ---------------------------------------------------------------- save.dat
void Make(TileMap *map, GameState *s) {
	cJSON *root = cJSON_CreateObject();
	cJSON_AddNumberToObject(root, "version", SAVE_VERSION);
	// Flags and variables as id lists (most are off / zero).
	cJSON *flags = cJSON_AddArrayToObject(root, "flags");
	for (size_t i = 0; i < s->flags.size(); i++)
		if (s->flags[i]) cJSON_AddItemToArray(flags, cJSON_CreateNumber((double)i));
	cJSON *vars = cJSON_AddObjectToObject(root, "vars");
	for (size_t i = 0; i < s->vars.size(); i++)
		if (s->vars[i]) cJSON_AddNumberToObject(vars, std::to_string(i).c_str(), s->vars[i]);
	cJSON *ss = cJSON_AddObjectToObject(root, "selfSwitches");
	for (auto &kv : s->self_switches)
		if (kv.second) cJSON_AddNumberToObject(ss, std::to_string(kv.first).c_str(), kv.second);
	cJSON *inv = cJSON_AddArrayToObject(root, "inventory");
	for (size_t i = 0; i < s->inventory.size(); i++)
		if (s->inventory[i]) cJSON_AddItemToArray(inv, cJSON_CreateNumber((double)i));
	cJSON *fol = cJSON_AddArrayToObject(root, "followers");
	for (int f : s->followers) cJSON_AddItemToArray(fol, cJSON_CreateNumber(f));
	cJSON *ft = cJSON_AddObjectToObject(root, "fastTravel");
	cJSON_AddBoolToObject(ft, "enabled", s->travel_enabled);
	cJSON *locs = cJSON_AddArrayToObject(ft, "locations");
	for (auto &l : s->travel_locations) {
		cJSON *o = cJSON_CreateObject();
		cJSON_AddNumberToObject(o, "zone", l.zone);
		cJSON_AddNumberToObject(o, "mapId", l.mapId);
		cJSON_AddNumberToObject(o, "x", l.x);
		cJSON_AddNumberToObject(o, "y", l.y);
		cJSON_AddNumberToObject(o, "direction", l.direction);
		cJSON_AddItemToArray(locs, o);
	}
	cJSON_AddNumberToObject(root, "plightTimeStart", s->plight_timer_start);
	cJSON_AddNumberToObject(root, "playTimeFrameCount", (double)s->play_frames);
	cJSON_AddNumberToObject(root, "plightBruteForceStartFrame", (double)s->bruteforce_start);
	{
		std::string img, name;
		int mode;
		uint8_t r, g, b;
		if (twm::overriding_wallpaper(img, name, mode, r, g, b)) {   // currentOverridingWallpaper
			cJSON *w = cJSON_AddObjectToObject(root, "currentOverridingWallpaper");
			cJSON_AddStringToObject(w, "imageFile", img.c_str());
			cJSON_AddStringToObject(w, "displayName", name.c_str());
			cJSON_AddNumberToObject(w, "mode", mode);
			cJSON *c = cJSON_AddObjectToObject(w, "bgColor");
			cJSON_AddNumberToObject(c, "r", r);
			cJSON_AddNumberToObject(c, "g", g);
			cJSON_AddNumberToObject(c, "b", b);
			cJSON_AddNumberToObject(c, "a", 255);
		}
	}
	cJSON_AddItemToObject(root, "pictureData", pictures().GetSaveData());
	audio::SaveData sd = audio::get_save_data();
	cJSON *snd = cJSON_AddObjectToObject(root, "soundSaveData");
	cJSON_AddStringToObject(snd, "BGMname", sd.bgm);
	cJSON_AddNumberToObject(snd, "BGMvol", sd.bgmVol);
	cJSON_AddNumberToObject(snd, "BGMpitch", sd.bgmPitch);
	cJSON_AddStringToObject(snd, "BGSname", sd.bgs);
	cJSON_AddNumberToObject(snd, "BGSvol", sd.bgsVol);
	cJSON_AddNumberToObject(snd, "BGSpitch", sd.bgsPitch);
	cJSON_AddItemToObject(root, "tileMapSaveData", map->GetSaveData());
	bool ok = write_json(SAVE_FILE, root);
	WritePerma(s);
	settings().Save();
	if (!twm::filesystem().FileExists(FAKESAVE)) {
		auto f = twm::make_file("save", "fakesave_filename", twm::PROG_DOCUMENT, { "save_progress_text" });
		f->moveRestricted = true;
		twm::filesystem().WriteFile(twm::Filesystem::MYGAMESONESHOT, f);
	}
	twm::save_desktop_and_fs();
	sys::log(ok ? "Game Saved! (map %d)\n" : "SAVE FAILED (map %d)\n", map->GetMapID());
}

bool Load(TileMap *map, GameState *s) {
	if (!Exists()) return true;
	cJSON *root = read_json(SAVE_FILE);
	const cJSON *tm = root ? cJSON_GetObjectItemCaseSensitive(root, "tileMapSaveData") : nullptr;
	if (!root || !cJSON_IsObject(tm) || !cJSON_IsArray(cJSON_GetObjectItemCaseSensitive(root, "flags"))) {
		cJSON_Delete(root);
		return false;
	}
	const cJSON *it;
	std::fill(s->flags.begin(), s->flags.end(), false);
	cJSON_ArrayForEach(it, cJSON_GetObjectItemCaseSensitive(root, "flags"))
		if (cJSON_IsNumber(it)) s->set_flag((int)it->valuedouble, true);
	std::fill(s->vars.begin(), s->vars.end(), 0);
	cJSON_ArrayForEach(it, cJSON_GetObjectItemCaseSensitive(root, "vars"))
		if (cJSON_IsNumber(it) && it->string) s->set_var(atoi(it->string), (int)it->valuedouble);
	s->self_switches.clear();
	cJSON_ArrayForEach(it, cJSON_GetObjectItemCaseSensitive(root, "selfSwitches"))
		if (cJSON_IsNumber(it) && it->string) s->self_switches[atoll(it->string)] = (int)it->valuedouble;
	const cJSON *ft = cJSON_GetObjectItemCaseSensitive(root, "fastTravel");
	s->travel_locations.clear();
	if (ft) {
		s->travel_enabled = !cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(ft, "enabled"));
		cJSON_ArrayForEach(it, cJSON_GetObjectItemCaseSensitive(ft, "locations")) {
			GameState::TravelLocation l;
			l.zone = jint(it, "zone", 0);
			l.mapId = jint(it, "mapId", 0);
			l.x = jint(it, "x", 0);
			l.y = jint(it, "y", 0);
			l.direction = jint(it, "direction", 2);
			s->travel_locations.push_back(l);
		}
	}
	pictures().LoadSaveData(cJSON_GetObjectItemCaseSensitive(root, "pictureData"));
	s->plight_timer_start = jint(root, "plightTimeStart", 0);
	s->bruteforce_start = (long long)jdbl(root, "plightBruteForceStartFrame", -1);
	s->play_frames = (long long)jdbl(root, "playTimeFrameCount", 0);
	if (const cJSON *w = cJSON_GetObjectItemCaseSensitive(root, "currentOverridingWallpaper")) {
		const cJSON *c = cJSON_GetObjectItemCaseSensitive(w, "bgColor");
		twm::override_wallpaper(jstr(w, "imageFile"), jint(w, "mode", 0), (uint8_t)jint(c, "r", 0), (uint8_t)jint(c, "g", 0),
		                        (uint8_t)jint(c, "b", 0));
	}
	std::fill(s->inventory.begin(), s->inventory.end(), false);
	cJSON_ArrayForEach(it, cJSON_GetObjectItemCaseSensitive(root, "inventory"))
		if (cJSON_IsNumber(it)) s->add_item((int)it->valuedouble);
	s->followers.clear();
	cJSON_ArrayForEach(it, cJSON_GetObjectItemCaseSensitive(root, "followers"))
		if (cJSON_IsNumber(it)) s->followers.push_back((int)it->valuedouble);
	map->LoadSaveData(tm);
	// Endings and the dream sequences start their own music.
	if (!s->flag(181) && !s->flag(183) && !s->flag(186) && !s->flag(188) && !s->flag(190)) {
		const cJSON *snd = cJSON_GetObjectItemCaseSensitive(root, "soundSaveData");
		if (snd) {
			audio::SaveData sd;
			snprintf(sd.bgm, sizeof(sd.bgm), "%s", jstr(snd, "BGMname").c_str());
			snprintf(sd.bgs, sizeof(sd.bgs), "%s", jstr(snd, "BGSname").c_str());
			sd.bgmVol = (float)jdbl(snd, "BGMvol", 1);
			sd.bgmPitch = (float)jdbl(snd, "BGMpitch", 1);
			sd.bgsVol = (float)jdbl(snd, "BGSvol", 1);
			sd.bgsPitch = (float)jdbl(snd, "BGSpitch", 1);
			audio::load_save_data(sd);
		}
	}
	if (!s->flag(198)) map->StartCommonEvent(42);   // "Load Save": welcome back / dreams
	window().ShiftBorderColorInstantly();
	twm::set_in_solstice(s->solstice_glitch_time());
	cJSON_Delete(root);
	sys::log("Save Loaded! (map %d)\n", map->GetMapID());
	return LoadPerma(s) || !sys::save_exists(PERMA_FILE);
}

void Erase() {
	if (sys::save_exists(SAVE_FILE)) {
		sys::save_delete(SAVE_FILE);
		sys::log("save erased\n");
	}
	if (desktop::ready()) twm::filesystem().Delete("/docs_foldername/mygames_foldername/oneshot_foldername/app_journal");
}

void ClearCorrupt() {
	sys::save_delete(SAVE_FILE);
	sys::save_delete(PERMA_FILE);
}

} // namespace save
