// save_folder.cpp - save files as plain files in a folder (see save_store.h).
// Each file is written to <name>.tmp and renamed over the old one, so a crash
// mid-write never leaves a half-written save behind.
#include "save_store.h"
#include "sys.h"
#include "user_files.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace sys {
namespace save_store {

static bool ends_with(const std::string &s, const char *suffix) {
	size_t n = strlen(suffix);
	return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

bool folder_load_all(const std::string &dir, Files &files) {
	for (const std::string &name : list_files(dir)) {
		if (ends_with(name, ".tmp")) continue;
		std::vector<uint8_t> data;
		if (read_abs(dir + "/" + name, data)) files[name].assign(data.begin(), data.end());
	}
	return true;
}

bool folder_commit(const std::string &dir, const Files &writes, const std::vector<std::string> &deletes) {
	bool ok = make_dirs(dir);
	for (auto &w : writes) {
		if (write_abs(dir + "/" + w.first, (const uint8_t *)w.second.data(), w.second.size())) continue;
		log("save: writing %s/%s failed\n", dir.c_str(), w.first.c_str());
		ok = false;
	}
	for (auto &d : deletes) remove_abs(dir + "/" + d);
	return ok;
}

#ifndef __PPU__
// The host keeps its saves in a folder: OSPS3_SAVEDIR, or "saves" next to
// the game data.
static const std::string &host_dir() {
	static std::string dir;
	if (dir.empty()) {
		const char *env = getenv("OSPS3_SAVEDIR");
		dir = env ? env : std::string(data_dir()) + "/saves";
	}
	return dir;
}

bool load_all(Files &files) { return folder_load_all(host_dir(), files); }
bool commit(const Files &writes, const std::vector<std::string> &deletes) { return folder_commit(host_dir(), writes, deletes); }
void pump() {}
const char *where() { return host_dir().c_str(); }
#endif

} // namespace save_store
} // namespace sys
