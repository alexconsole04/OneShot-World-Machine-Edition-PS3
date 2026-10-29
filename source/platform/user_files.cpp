// user_files.cpp - see user_files.h.
#include "user_files.h"
#include "sys.h"
#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __PPU__
#include <cell/cell_fs.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace sys {

static bool is_dir(const std::string &p) {
#ifdef __PPU__
	CellFsStat st;
	return cellFsStat(p.c_str(), &st) == CELL_FS_SUCCEEDED && (st.st_mode & CELL_FS_S_IFDIR);
#else
	struct stat st;
	return stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

bool make_dir(const std::string &p) {
	if (is_dir(p)) return true;
#ifdef __PPU__
	return cellFsMkdir(p.c_str(), CELL_FS_DEFAULT_CREATE_MODE_1) == CELL_FS_SUCCEEDED;
#else
	return mkdir(p.c_str(), 0755) == 0;
#endif
}

const std::string &wallpaper_dir() {
	static std::string dir;
	if (dir.empty()) {
#ifdef __PPU__
		dir = "/dev_hdd0/game/OSWM00001/USRDIR/wallpapers";
#else
		const char *env = getenv("OSPS3_WALLPAPERS");
		if (env) dir = env;
		else {
			std::string s = save_dir();
			size_t k = s.find_last_of('/');
			dir = (k == std::string::npos ? std::string(".") : s.substr(0, k)) + "/wallpapers";
		}
#endif
		make_dir(dir);
	}
	return dir;
}

static std::string lower(std::string s) {
	for (auto &c : s) c = (char)tolower((unsigned char)c);
	return s;
}

// Entries of dir (files and folders), names only.
static std::vector<std::string> entries(const std::string &dir) {
	std::vector<std::string> r;
	if (DIR *d = opendir(dir.c_str())) {
		while (struct dirent *e = readdir(d)) {
			std::string n = e->d_name;
			if (n != "." && n != "..") r.push_back(n);
		}
		closedir(d);
	}
	return r;
}

// dir/<name> whatever its case, or "" if there isn't one.
static std::string child_any_case(const std::string &dir, const char *name) {
	for (auto &n : entries(dir))
		if (lower(n) == name) return dir + "/" + n;
	return "";
}

static std::vector<std::string> usb_roots() {
	std::vector<std::string> roots, r;
#ifdef __PPU__
	for (int i = 0; i < 8; i++) {
		char p[32];
		snprintf(p, sizeof(p), "/dev_usb%03d", i);
		roots.push_back(p);
	}
#else
	if (const char *env = getenv("OSPS3_USB")) roots.push_back(env);   // host testing: a stand-in stick
#endif
	for (auto &root : roots)
		if (is_dir(root)) r.push_back(root);
	return r;
}

int usb_device_count() { return (int)usb_roots().size(); }

std::vector<std::string> usb_wallpaper_dirs() {
	std::vector<std::string> roots = usb_roots(), r;
	for (auto &root : roots) {
		std::string game = child_any_case(root, "oneshot");
		if (game.empty() || !is_dir(game)) continue;
		std::string w = child_any_case(game, "wallpapers");
		if (!w.empty() && is_dir(w)) r.push_back(w);
	}
	return r;
}

std::vector<std::string> list_files(const std::string &dir) {
	std::vector<std::string> r;
	for (auto &n : entries(dir))
		if (n[0] != '.' && !is_dir(dir + "/" + n)) r.push_back(n);
	return r;
}

bool read_abs(const std::string &path, std::vector<uint8_t> &out) {
	FILE *f = fopen(path.c_str(), "rb");
	if (!f) return false;
	// No big buffer on the stack: this runs on the decoding thread, whose
	// stack is 64 KB (a 64 KB buffer here overflowed it).
	out.clear();
	if (fseek(f, 0, SEEK_END) == 0) {
		long n = ftell(f);
		if (n > 0) out.reserve((size_t)n);
		fseek(f, 0, SEEK_SET);
	}
	std::vector<uint8_t> buf(16384);
	size_t n;
	while ((n = fread(buf.data(), 1, buf.size(), f)) > 0) out.insert(out.end(), buf.begin(), buf.begin() + n);
	fclose(f);
	return true;
}

bool write_abs(const std::string &path, const uint8_t *data, size_t size) {
	std::string tmp = path + ".tmp";
	FILE *f = fopen(tmp.c_str(), "wb");
	if (!f) return false;
	bool ok = fwrite(data, 1, size, f) == size;
	ok = fclose(f) == 0 && ok;
	if (ok) {
		remove_abs(path);
#ifdef __PPU__
		ok = cellFsRename(tmp.c_str(), path.c_str()) == CELL_FS_SUCCEEDED;
#else
		ok = rename(tmp.c_str(), path.c_str()) == 0;
#endif
	}
	if (!ok) remove_abs(tmp);
	return ok;
}

bool remove_abs(const std::string &path) {
#ifdef __PPU__
	return cellFsUnlink(path.c_str()) == CELL_FS_SUCCEEDED;
#else
	return unlink(path.c_str()) == 0;
#endif
}

} // namespace sys
