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

} // namespace twm
