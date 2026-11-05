// save_io.cpp - the save files (save.dat, p-settings.dat, settings.conf,
// desktop.dat, fs.dat, language.dat). They're all read into memory the
// first time one is asked for; after that reads and checks are answered from
// memory, and changes are written by a background thread (on the PS3 a save
// took the game thread 300+ ms). save_store.h says where they're kept.
#include "save_store.h"
#include "sys.h"
#include "thread.h"
#include <set>

namespace sys {

static Monitor *g_mon;
static save_store::Files g_files;              // as the game sees them
static std::set<std::string> g_dirty, g_gone;  // changed / deleted since the last write
static bool g_loaded, g_threadTried, g_threadOk, g_busy;

// The writing thread: everything changed so far, in one store operation.
static void write_changes() {
	save_store::Files writes;
	std::vector<std::string> deletes;
	{
		MonitorLock l(g_mon);
		for (auto &n : g_dirty) writes[n] = g_files[n];
		deletes.assign(g_gone.begin(), g_gone.end());
		g_dirty.clear();
		g_gone.clear();
		g_busy = true;
	}
	if (!save_store::commit(writes, deletes)) log("save: writing to %s failed\n", save_store::where());
	MonitorLock l(g_mon);
	g_busy = false;
	monitor_notify(g_mon);
}

static void writer(void *) {
	for (;;) {
		{
			MonitorLock l(g_mon);
			while (g_dirty.empty() && g_gone.empty()) monitor_wait(g_mon, 0);
		}
		write_changes();
	}
}

static void ensure_loaded() {
	if (g_loaded) return;
	g_loaded = true;
	g_mon = monitor_create("saves");
	uint64_t t0 = time_us();
	if (!save_store::load_all(g_files)) log("save: %s can't be read; starting without saves\n", save_store::where());
	log("save: %d files in %s (%.0f ms)\n", (int)g_files.size(), save_store::where(), (time_us() - t0) / 1000.0);
}

// After a change: wake the writer (started on the first change), or write
// now if there is no writer.
static void changed() {
	if (!g_threadTried) {
		g_threadTried = true;
		// The PS3's save utility runs on the calling thread's stack: give it room.
		g_threadOk = g_mon && thread_start(writer, nullptr, "save writer", -1, 256 * 1024);
		if (!g_threadOk) log("save: no writer thread, saving on the game thread\n");
	}
	if (g_threadOk) monitor_notify(g_mon);
	else write_changes();
}

void save_flush() {
	if (!g_loaded || !g_threadOk) return;
	for (;;) {
		{
			MonitorLock l(g_mon);
			if (g_dirty.empty() && g_gone.empty() && !g_busy) return;
			monitor_wait(g_mon, 5000);
		}
		save_store::pump();
	}
}

bool save_exists(const char *name) {
	ensure_loaded();
	MonitorLock l(g_mon);
	return g_files.count(name) != 0;
}

bool save_read(const char *name, std::string &out) {
	ensure_loaded();
	MonitorLock l(g_mon);
	auto it = g_files.find(name);
	if (it == g_files.end()) return false;
	out = it->second;
	return true;
}

bool save_write(const char *name, const std::string &data) {
	ensure_loaded();
	{
		MonitorLock l(g_mon);
		g_files[name] = data;
		g_dirty.insert(name);
		g_gone.erase(name);
	}
	changed();
	return true;
}

bool save_delete(const char *name) {
	ensure_loaded();
	{
		MonitorLock l(g_mon);
		if (!g_files.erase(name)) return true;
		g_dirty.erase(name);
		g_gone.insert(name);
	}
	changed();
	return true;
}

} // namespace sys
