// user_files.h - the player's own files, outside the game data: the custom
// wallpapers folder and USB sticks. Absolute paths, nothing cached.
#pragma once
#include <stdint.h>
#include <string>
#include <vector>

namespace sys {

// The folder custom wallpapers live in (made if missing):
// /dev_hdd0/OneShot/wallpapers on the PS3, outside the game's folder so
// deleting or reinstalling the game keeps them. (Before 1.01 they were in
// the game's USRDIR/wallpapers; they're moved the first time.)
const std::string &wallpaper_dir();
// Folders on USB sticks to import wallpapers from: <stick>/OneShot/wallpapers
// (any case) on each stick that has one.
std::vector<std::string> usb_wallpaper_dirs();
// How many USB storage devices are plugged in.
int usb_device_count();

// The port's own files that come in the pkg (USRDIR/port).
const std::string &port_dir();
// Size in bytes, or -1 if there's no such file.
long long file_size(const std::string &path);

// Regular files in dir (names only).
std::vector<std::string> list_files(const std::string &dir);
bool read_abs(const std::string &path, std::vector<uint8_t> &out);
bool write_abs(const std::string &path, const uint8_t *data, size_t size);
bool remove_abs(const std::string &path);
bool make_dir(const std::string &path);
bool make_dirs(const std::string &path);   // with any missing parents
bool rename_abs(const std::string &from, const std::string &to);

} // namespace sys
