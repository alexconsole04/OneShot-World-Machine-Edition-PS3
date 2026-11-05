// data_check.h - checks the player's copy of the game files against the list
// of every file the game has (USRDIR/port/game_files.txt, made by
// tools/assets/make_game_file_list.py), on a background thread at start-up.
// Each missing file is logged; the title screen and the desktop say so.
#pragma once

namespace data_check {

void start();          // after the game data folder is known
bool done();
int missing_count();   // once done(); 0 if the list itself couldn't be read
int checked_count();

} // namespace data_check
