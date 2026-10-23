// loc.h - LanguageManager: the game's languages (gamedata/loc/
// language_metadata.json), the chosen one (language.dat in the save folder)
// and its translated strings (the .po files under gamedata/loc/<code>/).
//
// Everything falls back to the original text, as on PC: English is the
// original, and a string a translation lacks shows in English.
#pragma once
#include <string>
#include <vector>

namespace loc {

struct Language {
	std::string code, displayName;
	std::string fontOS, fontGame, fontGameSmall;   // SpriteFont names in content/
	int fontOSScale = 2;
};

// Reads the metadata and the saved choice. Call once at startup, after the
// data folder is known.
void init();
const std::vector<Language> &languages();
const Language &current();
const std::string &code();   // GetCurrentLangCode
// False on the very first launch (no language.dat): the boot sequence then
// asks for a language.
bool was_set_on_startup();
// SetCurrentLangCode: loads that language's strings, switches fonts and
// (if save) writes language.dat.
void set(const std::string &code, bool save = true);
// Changes every time the language does (for anything caching text).
int generation();

// GetTWMLocString: menus, title screen, dialogs, boot screens.
const std::string &twm(const char *key);
// GetMapLocString: Show Text / Show Choices text (keys are the original
// text). pageId < 0 means a common event.
std::string map_text(int pageId, const std::string &original);
void load_map(int mapId);   // LoadMapLocFile, on every map load
// GetItemLocString.
std::string item_name(int id, const std::string &original);
std::string item_description(int id, const std::string &original);
// GetMapNameLocString / GetZoneLocString / GetMusicLocString / GetCgNameLocString.
std::string map_name(int mapId, const std::string &original);
std::string zone(const std::string &zoneId, const std::string &original);
std::string music(const std::string &trackId, const std::string &original);
std::string cg_name(const std::string &imageId, const std::string &original);
// GetContactLocString ("id:title"...), GetThemesLocString, GetWallpaperLocString,
// GetAchievementLocString ("id:title"...).
std::string contact(const std::string &key, const std::string &original);
std::string theme_name(const std::string &id, const std::string &original);
std::string wallpaper_name(const std::string &id, const std::string &original);
std::string achievement(const std::string &key, const std::string &original);

// gamedata/loc/<code>/<rel>, or the English one if the language lacks it
// (BootManager.SetSrcText, DocumentWindow).
std::string localized_path(const char *rel);

// Localized texture: content/<dir>/<code>/<name>.xnb if the language has
// one (CacheAndManager.GetTexture). Returns the name to load.
std::string texture_name(const std::string &name);

} // namespace loc
