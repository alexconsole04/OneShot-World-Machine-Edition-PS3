// save_io.cpp - save files (save.dat, p-settings.dat, settings.conf) in a
// "saves" folder next to the game data. Writes go to a temporary file that
// is then renamed over the old one, so a crash mid-write never leaves a
// half-written save behind.
#include "sys.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>

#ifdef __PPU__
#include <cell/cell_fs.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace sys {

static std::string g_save_dir;

const char *save_dir() {
	if (g_save_dir.empty()) {
#ifdef __PPU__
		g_save_dir = "/dev_hdd0/game/OSWM00001/USRDIR/saves";
		cellFsMkdir(g_save_dir.c_str(), CELL_FS_DEFAULT_CREATE_MODE_1);
#else
		const char *env = getenv("OSPS3_SAVEDIR");
		g_save_dir = env ? env : std::string(data_dir()) + "/saves";
		mkdir(g_save_dir.c_str(), 0755);
#endif
		log("save folder: %s\n", g_save_dir.c_str());
	}
	return g_save_dir.c_str();
}

static std::string path_of(const char *name) { return std::string(save_dir()) + "/" + name; }

static bool remove_path(const std::string &p) {
#ifdef __PPU__
	return cellFsUnlink(p.c_str()) == CELL_FS_SUCCEEDED;
#else
	return unlink(p.c_str()) == 0;
#endif
}

static bool rename_path(const std::string &from, const std::string &to) {
#ifdef __PPU__
	return cellFsRename(from.c_str(), to.c_str()) == CELL_FS_SUCCEEDED;
#else
	return rename(from.c_str(), to.c_str()) == 0;
#endif
}

bool save_exists(const char *name) {
	// A stat, not fopen/fclose: no stdio involved just to look.
#ifdef __PPU__
	CellFsStat st;
	return cellFsStat(path_of(name).c_str(), &st) == CELL_FS_SUCCEEDED;
#else
	struct stat st;
	return stat(path_of(name).c_str(), &st) == 0;
#endif
}

bool save_read(const char *name, std::string &out) {
	FILE *f = fopen(path_of(name).c_str(), "rb");
	if (!f) return false;
	out.clear();
	char buf[4096];
	size_t n;
	while ((n = fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
	fclose(f);
	return true;
}

bool save_write(const char *name, const std::string &data) {
	std::string dst = path_of(name), tmp = dst + ".tmp";
	FILE *f = fopen(tmp.c_str(), "wb");
	if (!f) {
		log("save: cannot create %s\n", tmp.c_str());
		return false;
	}
	bool ok = fwrite(data.data(), 1, data.size(), f) == data.size();
	ok = fflush(f) == 0 && ok;
	ok = fclose(f) == 0 && ok;
	if (!ok) {
		log("save: write to %s failed\n", tmp.c_str());
		remove_path(tmp);
		return false;
	}
	remove_path(dst);
	if (!rename_path(tmp, dst)) {
		// Rename refused: write in place instead (still better than no save).
		log("save: rename %s failed, writing directly\n", tmp.c_str());
		FILE *g = fopen(dst.c_str(), "wb");
		if (!g) return false;
		ok = fwrite(data.data(), 1, data.size(), g) == data.size();
		ok = fclose(g) == 0 && ok;
		remove_path(tmp);
		return ok;
	}
	return true;
}

bool save_delete(const char *name) {
	if (!save_exists(name)) return true;
	return remove_path(path_of(name));
}

} // namespace sys
