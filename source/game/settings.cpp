#include "settings.h"
#include "../platform/sys.h"
#include "cJSON.h"
#include <string>

Settings &settings() {
	static Settings s;
	return s;
}

static void get_bool(const cJSON *o, const char *k, bool &v) {
	const cJSON *j = cJSON_GetObjectItemCaseSensitive(o, k);
	if (cJSON_IsBool(j)) v = cJSON_IsTrue(j);
}

void Settings::Load() {
	std::string text;
	if (!sys::save_read("settings.conf", text)) return;
	cJSON *root = cJSON_Parse(text.c_str());
	if (!root) {
		sys::log("settings: settings.conf is damaged, using defaults\n");
		return;
	}
	// Same field names as the PC build's SettingsSaveData.
	get_bool(root, "isDefaultMovementRun", defaultMovementRun);
	get_bool(root, "isColorblindMode", colorblindMode);
	get_bool(root, "isAutomashEnabled", automash);
	get_bool(root, "isControllerVibrationEnabled", vibration);
	get_bool(root, "isScreenTearEnabled", screenTear);
	get_bool(root, "isChromaticAberrationEnabled", chromaAberration);
	const cJSON *sc = cJSON_GetObjectItemCaseSensitive(root, "ps3ScalingMode");
	// 2 (integer) and 3 (direct) were dropped in m14: they read as sharp.
	if (cJSON_IsNumber(sc)) scaling = sc->valueint == 1 ? 1 : 0;
	get_bool(root, "fullscreen", fullscreen);
	get_bool(root, "isFullscreenBorderEnabled", fullscreenBorder);
	cJSON_Delete(root);
}

void Settings::Save() const {
	cJSON *root = cJSON_CreateObject();
	cJSON_AddBoolToObject(root, "isDefaultMovementRun", defaultMovementRun);
	cJSON_AddBoolToObject(root, "isColorblindMode", colorblindMode);
	cJSON_AddBoolToObject(root, "isAutomashEnabled", automash);
	cJSON_AddBoolToObject(root, "isControllerVibrationEnabled", vibration);
	cJSON_AddBoolToObject(root, "isScreenTearEnabled", screenTear);
	cJSON_AddBoolToObject(root, "isChromaticAberrationEnabled", chromaAberration);
	cJSON_AddNumberToObject(root, "ps3ScalingMode", scaling);
	cJSON_AddBoolToObject(root, "fullscreen", fullscreen);
	cJSON_AddBoolToObject(root, "isFullscreenBorderEnabled", fullscreenBorder);
	// The PC's own scaling fields (SettingsMenu.ScalingMode: 1 smooth, 2 sharp).
	cJSON_AddNumberToObject(root, "fullscreenScaling", scaling == 1 ? 1 : 2);
	cJSON_AddBoolToObject(root, "isFullscreenScalingSmooth", scaling == 1);
	char *s = cJSON_PrintUnformatted(root);
	if (s) {
		sys::save_write("settings.conf", s);
		cJSON_free(s);
	}
	cJSON_Delete(root);
}

#include "../platform/input.h"
bool auto_mashing() { return settings().automash && input::held(input::BTN_MASHTEXT); }
