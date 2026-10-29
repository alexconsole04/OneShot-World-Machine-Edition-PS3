// wm_internal.h - the WindowManager services the desktop's own parts use.
#pragma once
#include "wm.h"
#include "fs.h"
#include "icons.h"
#include "unlocks.h"
#include "window.h"

namespace twm {

void show_system_modal(int type, const std::string &msg, std::function<void(int)> callback = nullptr, bool sound = true,
                       bool canAutomash = false);
bool system_modal_open();
bool desktop_ready();
ToastManager &toasts();
UnlockManager &unlocks();
DesktopGrid &desktop_grid();
void save_desktop();
void add_window(Window *w);   // takes ownership (deleted at once if refused)
bool run_file(Node *node);
void begin_drag(DraggedItem *item);   // takes ownership
// UnlockContactsApp / UnlockJukeboxApp / UnlockGalleryApp / UnlockAchievementsApp
void unlock_app(Program p);
void flash(Color c, int duration, float strength);
bool clover_equipped();
Window *find_window(bool (*pred)(Window *));
bool game_window_open();
// The Customize app's settings (kept in desktop.dat, the PC's fields).
int brightness();              // Gamma, 50..150 (%)
void set_brightness(int v);
int bgm_volume();
int sfx_volume();
void set_volumes(int bgm, int sfx);
void hide_all_ui();            // View Wallpaper: the wallpaper alone until a button press
// Restart the World Machine (after a fade): a language change, or Erase All
// Data. main.cpp carries it out (desktop::take_restart_request).
enum { RESTART_NONE = 0, RESTART_LANGUAGE = 1, RESTART_ERASE = 2 };
void request_restart(int kind);
// The player's own wallpapers: read the folder again and update the
// Wallpapers folder (after an import), or delete one.
void refresh_custom_wallpapers();
void remove_custom_wallpaper(const std::string &id);

} // namespace twm
