// loc.h - LanguageManager.GetTWMLocString for English: the strings in
// gamedata/loc/en/twm/twm_strs.po (menus, title screen, dialogs).
#pragma once
#include <string>

namespace loc {
// The string for key, or a built-in English fallback, or the key itself.
const std::string &twm(const char *key);
}
