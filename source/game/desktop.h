// desktop.h - the World Machine desktop (WindowManager, TWMDesktopManager,
// TWMTaskbar, TWMWindow): wallpaper, desktop icons, taskbar with clock,
// and windows you can drag, minimize, maximize and close with the cursor
// (right stick + R2).
//
// Stage 1: the OneShot window and the Shut Down icon. The other apps
// (folders, documents, customization, ...) come later.
//
// Coordinates are desktop pixels (640x360 on a 16:9 TV), drawn at 2x into
// the 1280x720 desktop draw space, which is fitted to the TV.
#pragma once

namespace desktop {
void init();             // WindowManager(): themes, wallpaper, icons, desktop.dat
bool ready();
// The wallpaper can be drawn now (a custom one is decoded in the
// background): the boot waits on its logo for it.
bool wallpaper_ready();
void update();           // cursor, taskbar, windows (and the game inside its window), icons
void draw();             // everything but the cursor
void draw_cursor();      // last, over dialogs
bool quit_requested();   // Shut Down confirmed
void close_all();        // a language change: every window closes (no saving)
// Debug starts and the map switcher: make sure the OneShot window is there.
void ensure_game_window(bool maximized);
bool game_window_open();
// A restart the Customize app asked for (1 language, 2 erase all data), once.
int take_restart_request();
// Erase All Data: deletes every save but language.dat and settings.conf and
// forgets the desktop, so the next init starts as a first launch.
void erase_all_data();
} // namespace desktop
