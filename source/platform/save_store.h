// save_store.h - where the save files are kept. save_io.cpp holds them in
// memory and calls these to read them all once and to write changes.
//   PS3:  the system's saved data (ps3/save_data_ps3.cpp), one save named
//         OSWM00001-SAVEDATA in the XMB's Saved Data Utility.
//   Host: a folder (save_folder.cpp).
#pragma once
#include <map>
#include <string>
#include <vector>

namespace sys {
namespace save_store {

using Files = std::map<std::string, std::string>;   // name -> contents

// Every save file. False if the store can't be used at all.
bool load_all(Files &files);
// Writes these files and deletes those (one operation).
bool commit(const Files &writes, const std::vector<std::string> &deletes);
// Called while the game thread waits for a write to finish (the PS3's system
// utilities need their callbacks serviced meanwhile).
void pump();
// For the log: where the saves are.
const char *where();

// A plain folder of save files (the host's store; the PS3's old saves and
// its fallback). Names ending in .tmp are leftovers and are skipped.
bool folder_load_all(const std::string &dir, Files &files);
bool folder_commit(const std::string &dir, const Files &writes, const std::vector<std::string> &deletes);

} // namespace save_store
} // namespace sys
