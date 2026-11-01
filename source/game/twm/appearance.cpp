// appearance.cpp - the desktop's looks: themes (and the rainbow theme's hue
// cycle), the player's own wallpapers in the Wallpapers folder, and the
// Customize app's brightness and volume.
#include "wm_state.h"
#include "apps/custom_wallpapers.h"
#include "../../engine/audio.h"
#include "../../engine/gm.h"
#include "../../platform/sys.h"
#include "../core/json_file.h"
#include <math.h>

namespace twm {

// ===================================================================== themes

std::vector<Theme> g_themes;
static std::vector<std::pair<Color, Color>> g_themeOriginals;   // rainbow: colours before the hue cycle
static float g_rainbowOffset;
std::string g_themeId = "purple";
static Theme g_fallbackTheme;

void load_themes() {
	g_themes.clear();
	if (cJSON *o = parse_json_file("gamedata/twm/themes_metadata.json")) {
		const cJSON *it;
		cJSON_ArrayForEach(it, cJSON_GetObjectItemCaseSensitive(o, "themes")) {
			Theme t;
			const cJSON *id = cJSON_GetObjectItemCaseSensitive(it, "id"), *dn = cJSON_GetObjectItemCaseSensitive(it, "displayName");
			if (!cJSON_IsString(id)) continue;
			t.id = id->valuestring;
			t.displayName = cJSON_IsString(dn) ? dn->valuestring : t.id;
			auto col = [&](const char *k, Color &c) {
				const cJSON *v = cJSON_GetObjectItemCaseSensitive(it, k);
				if (!v) return;
				c = Color((int)cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(v, "r")),
				          (int)cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(v, "g")),
				          (int)cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(v, "b")));
			};
			col("primary", t.primary);
			col("primaryVariant", t.variant);
			col("background", t.background);
			bool dup = false;
			for (auto &x : g_themes) dup |= x.id == t.id;
			if (!dup) g_themes.push_back(t);
		}
		cJSON_Delete(o);
	}
	if (g_themes.empty()) sys::log("twm: gamedata/twm/themes_metadata.json missing, using the purple theme\n");
	g_themeOriginals.clear();
	for (auto &t : g_themes) g_themeOriginals.push_back({ t.primary, t.variant });
}

const Theme *theme_by_id(const std::string &id) {
	for (auto &t : g_themes)
		if (t.id == id) return &t;
	return nullptr;
}
const Theme &theme() {
	const Theme *t = theme_by_id(g_themeId);
	return t ? *t : g_fallbackTheme;
}
const std::vector<Theme> &themes() { return g_themes; }

static void hue(Color &c, Color orig, float h) {   // MathHelper.ApplyHue
	const float k = 0.57735f;
	float v[3] = { orig.r / 255.0f, orig.g / 255.0f, orig.b / 255.0f };
	float cs = cosf(h), sn = sinf(h), d = k * (v[0] + v[1] + v[2]) * (1 - cs);
	float cr[3] = { k * v[2] - k * v[1], k * v[0] - k * v[2], k * v[1] - k * v[0] };
	uint8_t out[3];
	for (int i = 0; i < 3; i++) {
		float x = v[i] * cs + cr[i] * sn + k * d;
		out[i] = (uint8_t)((x < 0 ? 0 : x > 1 ? 1 : x) * 255);
	}
	c.r = out[0]; c.g = out[1]; c.b = out[2];
}

void update_themes() {   // TWMTheme.Update: the rainbow theme cycles its hue
	g_rainbowOffset += 0.02f;
	if (g_rainbowOffset > 6.2831853f) g_rainbowOffset -= 6.2831853f;
	for (size_t i = 0; i < g_themes.size(); i++)
		if (g_themes[i].id == "rainbow") {
			hue(g_themes[i].primary, g_themeOriginals[i].first, g_rainbowOffset);
			hue(g_themes[i].variant, g_themeOriginals[i].second, g_rainbowOffset);
		}
}

// ===================================================================== the player's own wallpapers

void apply_custom_wallpapers(bool rebuildFolder) {
	std::vector<WallpaperInfo> v;
	g_fs.customWallpapers.clear();
	for (auto &c : custom_wallpapers()) {
		WallpaperInfo w;
		w.imageFile = c.id;
		w.displayName = c.displayName;
		w.mode = WallpaperInfo::STRETCH;   // the texture is made at the screen's size
		w.bg = Color(0, 0, 0);
		v.push_back(w);
		g_fs.customWallpapers.push_back(c.id);
	}
	g_desktop->SetCustomWallpapers(v);
	if (!rebuildFolder) return;
	if (g_tutStep == TUT_COMPLETE) g_fs.CreateWallpaperFiles(unlocks().wallpapers);
	// The picture on the desktop went away: back to the default one.
	if (!g_desktop->IsOverridden() && !g_desktop->WallpaperMeta(g_desktop->Current().imageFile)) g_desktop->SetWallpaper("default");
	save_desktop_and_fs();
}

void refresh_custom_wallpapers() {
	scan_custom_wallpapers();
	apply_custom_wallpapers(true);
}

void remove_custom_wallpaper(const std::string &id) {
	delete_custom_wallpaper(id);
	apply_custom_wallpapers(true);
}

// ===================================================================== Customize services

bool game_window_open() { return game_window() != nullptr; }
int brightness() { return g_gamma; }
void set_brightness(int v) { g_gamma = v < 50 ? 50 : v > 150 ? 150 : v; save_desktop(); }
int bgm_volume() { return g_bgmVol; }
int sfx_volume() { return g_sfxVol; }
void set_volumes(int bgm, int sfx) {
	g_bgmVol = bgm;
	g_sfxVol = sfx;
	audio::set_volumes(bgm, sfx);
	save_desktop();
}
void hide_all_ui() { g_hideUI = true; }

void request_restart(int kind) {
	// WindowManager.ShowLanguageSelect: fade out, then restart.
	fade(128, 255, 120, [kind] {
		for (auto &w : g_windows)
			if (w->onClose) w->onClose(w.get());
		g_restartRequest = kind;
	});
}

// HueShader's "gamma": every colour times Brightness/100, done here as one
// pass over the finished frame.
void draw_brightness() {
	if (g_gamma == 100) return;
	gfx::Shade sh;
	gfx::Blend b = gfx::Blend::Multiply;
	float f = g_gamma / 100.0f;
	if (f > 1.0f) { b = gfx::Blend::Brighten; f -= 1.0f; }
	sh.r = sh.g = sh.b = f;
	gfx::clip_off();
	gfx::draw(gm::white(), 0, 0, (float)gfx::screen_w(), (float)gfx::screen_h(), 0, 0, 1, 1, sh, b);
}

} // namespace twm
