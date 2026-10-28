// wm.h - what the game (scripts, saves, menus) asks of the World Machine:
// WindowManager / UnlockManager / TWMFilesystem calls made from outside the
// desktop. Light on includes so game code can use it.
#pragma once
#include "../../engine/types.h"
#include <string>

namespace twm {

class Filesystem;
struct WallpaperInfo;
Filesystem &filesystem();

// UnlockManager
void unlock_profile(const std::string &id);
void unlock_wallpaper(const std::string &id);
void unlock_theme(const std::string &id);
void unlock_achievement(const std::string &id);
void unlock_cg(const std::string &imageName);   // every picture shown
bool profile_unlocked(const std::string &id);
bool wallpaper_unlocked(const std::string &id);
// FlagManager's computed flags (401-469: unlocked wallpapers, profiles,
// themes): -1 when the flag isn't one of them.
int computed_flag(int id);

// The OneShot window
bool game_maximized();
void set_game_maximized(bool on);   // the Fullscreen setting / Graphics.fullscreen
Vec2 game_window_pos();             // desktop pixels (TWMWindow.Pos)
void set_game_window_pos(Vec2 p);
bool game_window_modal_open();
// WindowManager
bool journal_open();
void set_journal_page(const std::string &page);
void deploy_niko(bool isSolstice, bool isEntityNiko);
void override_wallpaper(const std::string &image, int mode, uint8_t r, uint8_t g, uint8_t b);
void restore_wallpaper();
void set_wallpaper(const std::string &id);
bool wallpaper_overridden();
// The overriding wallpaper, for the save (GetWallpaperInfoSaveData).
bool overriding_wallpaper(std::string &image, std::string &name, int &mode, uint8_t &r, uint8_t &g, uint8_t &b);
void set_in_solstice(bool on);
void save_desktop_and_fs();
Vec2 screen_size();                 // desktop pixels (WindowManager.ScreenSize)
// The cursor over the game picture, in game pixels (320x240), when it can
// act on the game (OneshotWindow.canMouseInteract): collectibles.
bool game_cursor(Vec2 *gamePx);
bool cursor_clicked();
// A theme's colours (current theme if the id is empty or unknown).
// The Timer app's playthrough timer (TitleScreen starts/resets it, the ending stops it).
void start_playthrough_timer();
void stop_playthrough_timer();
void reset_playthrough_timer();
void theme_rgb(const std::string &id, uint8_t primary[3], uint8_t background[3]);

} // namespace twm
