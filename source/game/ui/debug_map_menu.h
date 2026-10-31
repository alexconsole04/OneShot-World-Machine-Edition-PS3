// debug_map_menu.h - debug map switcher: every map in the game, grouped in
// tabs by area (the map colours of oneshot_map_colors.json), with the
// places other maps' Transfer Player commands arrive at as entry points.
//
// Opened from the debug overlay (SELECT, then L1). While it is open the game
// is paused. Choosing a map stops every running script and screen effect
// before loading it (OneshotWindow::DebugTeleport), so a cutscene or an
// exit transition from the old map can't carry on into the new one.
#pragma once

namespace debug_map_menu {
void open(int currentMapId);
bool is_open();
void update();   // call instead of the game's update while open
void draw();     // screen space, after everything else
} // namespace debug_map_menu
