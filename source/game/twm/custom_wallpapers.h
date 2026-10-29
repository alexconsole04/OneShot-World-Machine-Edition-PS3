// custom_wallpapers.h - the player's own wallpapers (this port's feature; the
// PC version has none). PNG/JPG/BMP pictures in the game's wallpapers folder
// (USRDIR/wallpapers), imported from a USB stick's OneShot/wallpapers folder
// or copied there by FTP. Each shows in the Wallpapers folder next to the
// game's own, filling the screen (scaled and cropped to fit).
#pragma once
#include <string>
#include <vector>

namespace twm {

struct CustomWallpaper {
	std::string id;            // "custom:<file name>" (the wallpaper id and the fs file's name)
	std::string file;          // file name in the wallpapers folder
	std::string displayName;   // the file name without its extension
};

static inline bool is_custom_wallpaper(const std::string &id) { return id.compare(0, 7, "custom:") == 0; }

// Reads the wallpapers folder again. Also installs the texture source that
// draws them (once).
void scan_custom_wallpapers();
const std::vector<CustomWallpaper> &custom_wallpapers();
std::string custom_wallpaper_name(const std::string &id);

struct ImportResult { int devices = 0, sticks = 0, imported = 0, existing = 0, unreadable = 0; };   // sticks: with the folder
// Copies new pictures from every USB stick's OneShot/wallpapers folder.
ImportResult import_custom_wallpapers();
// Removes the picture from the wallpapers folder.
bool delete_custom_wallpaper(const std::string &id);

} // namespace twm
