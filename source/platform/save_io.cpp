// save_io.cpp - save files (save.dat, p-settings.dat, settings.conf) in a
// "saves" folder next to the game data. Writes go to a temporary file that
// is then renamed over the old one, so a crash mid-write never leaves a
// half-written save behind. The writing happens on a background thread (on
// the PS3 a save took the game thread 300+ ms); reads see what's queued.
#include "sys.h"
#include "thread.h"
#include <deque>
#include <utility>
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

// ------------------------------------------------------------------ the writing thread
static Monitor *g_wmon;
static std::deque<std::pair<std::string, std::string>> g_wqueue;   // waiting: (name, data)
static std::pair<std::string, std::string> g_writing;               // being written now
static bool g_busy, g_wtried, g_wok;
static bool write_now(const char *name, const std::string &data);

static void writer(void *) {
	for (;;) {
		{
			MonitorLock l(g_wmon);
			while (g_wqueue.empty()) monitor_wait(g_wmon, 0);
			g_writing = std::move(g_wqueue.front());
			g_wqueue.pop_front();
			g_busy = true;
		}
		if (!write_now(g_writing.first.c_str(), g_writing.second)) log("save: writing %s failed\n", g_writing.first.c_str());
		MonitorLock l(g_wmon);
		g_busy = false;
		g_writing = {};
		monitor_notify(g_wmon);
	}
}

// The newest data queued (or being written) for name.
static bool pending_data(const char *name, std::string *out) {
	if (!g_wok) return false;
	MonitorLock l(g_wmon);
	for (auto it = g_wqueue.rbegin(); it != g_wqueue.rend(); ++it)
		if (it->first == name) { if (out) *out = it->second; return true; }
	if (g_busy && g_writing.first == name) { if (out) *out = g_writing.second; return true; }
	return false;
}

void save_flush() {
	if (!g_wok) return;
	MonitorLock l(g_wmon);
	while (!g_wqueue.empty() || g_busy) monitor_wait(g_wmon, 5000);
}

bool save_write(const char *name, const std::string &data) {
	if (!g_wtried) {
		g_wtried = true;
		save_dir();   // made (and logged) on this thread
		g_wmon = monitor_create("save writer");
		g_wok = g_wmon && thread_start(writer, nullptr, "save writer", -1);
		if (!g_wok) log("save: no writer thread, saving on the game thread\n");
	}
	if (!g_wok) return write_now(name, data);
	MonitorLock l(g_wmon);
	for (auto &q : g_wqueue)
		if (q.first == name) { q.second = data; return true; }   // only the newest gets written
	g_wqueue.emplace_back(name, data);
	monitor_notify(g_wmon);
	return true;
}

bool save_exists(const char *name) {
	if (pending_data(name, nullptr)) return true;
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
	if (pending_data(name, &out)) return true;
	FILE *f = fopen(path_of(name).c_str(), "rb");
	if (!f) return false;
	out.clear();
	char buf[4096];
	size_t n;
	while ((n = fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
	fclose(f);
	return true;
}

static bool write_now(const char *name, const std::string &data) {
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
	save_flush();   // a queued write mustn't bring it back
	if (!save_exists(name)) return true;
	return remove_path(path_of(name));
}

} // namespace sys
