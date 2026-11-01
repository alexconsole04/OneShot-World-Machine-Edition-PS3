// desktop_saves.cpp - desktop.dat and fs.dat: writing them (only once the
// tutorial is complete) and reading desktop.dat back.
#include "wm_state.h"
#include "../../engine/audio.h"
#include "../../engine/gm.h"
#include "../../platform/sys.h"
#include "../core/json_file.h"
#include "../core/settings.h"

namespace twm {

// ===================================================================== saving

void save_desktop() { g_desktopDirty = true; }
void save_desktop_and_fs() { g_desktopDirty = g_fsDirty = true; }

void write_saves() {
	if (g_tutStep != TUT_COMPLETE) return;   // nothing is saved during a tutorial (it runs on a stand-in desktop)
	if (g_fsDirty) {
		g_fsDirty = false;
		std::string text = g_fs.Save();
		if (!sys::save_write("fs.dat", text)) sys::log("twm: couldn't write fs.dat\n");
	}
	if (!g_desktopDirty) return;
	g_desktopDirty = false;
	// FilesystemSaveManager.DesktopSaveData, the PC's field names.
	cJSON *o = cJSON_CreateObject();
	cJSON *pos = cJSON_AddObjectToObject(o, "iconPositions");
	for (auto &p : g_desktop->Layout()) {
		cJSON *v = cJSON_AddObjectToObject(pos, p.first.c_str());
		cJSON_AddNumberToObject(v, "X", p.second.x);
		cJSON_AddNumberToObject(v, "Y", p.second.y);
	}
	cJSON_AddStringToObject(o, "currentWallpaper", g_desktop->Current().imageFile.c_str());
	cJSON_AddStringToObject(o, "currentTheme", g_themeId.c_str());
	unlocks().PopulateSaveData(o);
	cJSON_AddBoolToObject(o, "tutorialCompleted", g_tutStep == TUT_COMPLETE);
	cJSON_AddBoolToObject(o, "inSolstice", g_desktop->inSolstice);
	cJSON_AddBoolToObject(o, "isSystemScalingSmooth", settings().scaling == gm::SCALE_SMOOTH);
	cJSON_AddNumberToObject(o, "Gamma", g_gamma);
	cJSON_AddNumberToObject(o, "BGMVolume", g_bgmVol);
	cJSON_AddNumberToObject(o, "SFXVolume", g_sfxVol);
	cJSON_AddBoolToObject(o, "isVsyncEnabled", 1);
	cJSON_AddBoolToObject(o, "isBadgeToastsEnabled", unlocks().badgeToastsEnabled);
	cJSON_AddBoolToObject(o, "isFullscreen", 1);
	cJSON *dr = cJSON_AddObjectToObject(o, "DrawResolution");
	cJSON_AddNumberToObject(dr, "X", space().x);
	cJSON_AddNumberToObject(dr, "Y", space().y);
	char *s = cJSON_PrintUnformatted(o);
	if (s) {
		if (!sys::save_write("desktop.dat", s)) sys::log("twm: couldn't write desktop.dat\n");
		cJSON_free(s);
	}
	cJSON_Delete(o);
}

bool load_desktop(bool *tutorialCompleted) {
	std::string text;
	if (!sys::save_read("desktop.dat", text)) return false;
	cJSON *o = cJSON_Parse(text.c_str());
	if (!o) {
		sys::log("twm: desktop.dat is damaged, using the default layout\n");
		return false;
	}
	std::vector<std::pair<std::string, Vec2>> layout;
	const cJSON *it;
	cJSON_ArrayForEach(it, cJSON_GetObjectItemCaseSensitive(o, "iconPositions"))
		layout.push_back({ it->string ? it->string : "", Vec2((int)cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(it, "X")),
		                                                      (int)cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(it, "Y"))) });
	g_desktop->SetLayout(layout);
	const cJSON *wp = cJSON_GetObjectItemCaseSensitive(o, "currentWallpaper");
	g_desktop->SetWallpaper(cJSON_IsString(wp) && g_desktop->WallpaperMeta(wp->valuestring) ? wp->valuestring : "default");
	g_desktop->inSolstice = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(o, "inSolstice"));
	if (tutorialCompleted) {
		const cJSON *tc = cJSON_GetObjectItemCaseSensitive(o, "tutorialCompleted");
		*tutorialCompleted = !cJSON_IsBool(tc) || cJSON_IsTrue(tc);
	}
	auto num = [o](const char *k, int def, int lo, int hi) {
		const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
		int n = cJSON_IsNumber(v) ? (int)v->valuedouble : def;
		return n < lo ? lo : n > hi ? hi : n;
	};
	g_gamma = num("Gamma", 100, 50, 150);
	g_bgmVol = num("BGMVolume", 100, 0, 100);
	g_sfxVol = num("SFXVolume", 100, 0, 100);
	audio::set_volumes(g_bgmVol, g_sfxVol);
	const cJSON *bt = cJSON_GetObjectItemCaseSensitive(o, "isBadgeToastsEnabled");
	if (cJSON_IsBool(bt)) unlocks().badgeToastsEnabled = cJSON_IsTrue(bt);
	unlocks().LoadSaveData(o);
	const cJSON *th = cJSON_GetObjectItemCaseSensitive(o, "currentTheme");
	if (cJSON_IsString(th) && theme_by_id(th->valuestring)) g_themeId = th->valuestring;
	cJSON_Delete(o);
	g_fs.CreateWallpaperFiles(unlocks().wallpapers);
	g_fs.CreateThemeFiles(unlocks().themes);
	return true;
}

} // namespace twm
