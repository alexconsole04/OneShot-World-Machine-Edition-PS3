// log_file.cpp - the on-disk log (oneshot_log.txt).
//
// Lines are queued and written by a background thread, so a slow write to
// the hard drive never holds up a frame (on the PS3, writing and flushing
// each line on the game thread could take a third of a second). The writer
// flushes after every batch, so at most the last few milliseconds of lines
// are lost if the console is switched off. Lines logged before the file is
// opened are buffered and written first.
#include "sys.h"
#include "thread.h"
#include <stdio.h>
#include <string.h>
#include <string>

namespace sys {

static FILE *g_file;
static std::string g_pending;   // waiting for the writer (or for the file to open)
static bool g_tried, g_thread, g_writing;
static Monitor *g_mon;
static uint64_t g_t0;
// Long sessions: past LOG_CAP bytes the file is rotated (this part becomes
// oneshot_log_prev.txt), so the log never grows without limit.
static const size_t LOG_CAP = 4u * 1024 * 1024;
static size_t g_written;
static std::string g_path, g_prevPath;

static void rotate() {
	fclose(g_file);
	remove(g_prevPath.c_str());
	rename(g_path.c_str(), g_prevPath.c_str());
	g_file = fopen(g_path.c_str(), "w");
	g_written = 0;
	if (g_file) fputs("(log continued; the earlier part of this session is in oneshot_log_prev.txt)\n", g_file);
}

static Monitor *mon() {
	if (!g_mon) g_mon = monitor_create("log");   // first use is on the main thread, before any other thread
	return g_mon;
}

static void writer(void *) {
	std::string batch;
	for (;;) {
		monitor_lock(mon());
		while (g_pending.empty()) monitor_wait(mon(), 0);
		batch.swap(g_pending);
		g_writing = true;
		monitor_unlock(mon());
		if (g_file) {
			fputs(batch.c_str(), g_file);
			fflush(g_file);
			g_written += batch.size();
			if (g_written > LOG_CAP) { monitor_lock(mon()); rotate(); monitor_unlock(mon()); }
		}
		batch.clear();
		monitor_lock(mon());
		g_writing = false;
		monitor_notify(mon());   // log_flush may be waiting
		monitor_unlock(mon());
	}
}

void log_file_open(const char *path, const char *prev_path) {
	monitor_lock(mon());
	if (!g_tried) {
		g_tried = true;
		g_path = path;
		g_prevPath = prev_path;
		remove(prev_path);
		rename(path, prev_path);
		g_file = fopen(path, "w");
		if (g_file) {
			fputs(g_pending.c_str(), g_file);
			fflush(g_file);
			g_written = g_pending.size();
		}
		g_pending.clear();
		if (g_file) g_thread = thread_start(writer, nullptr, "log writer", -1);
	}
	monitor_unlock(mon());
}

void log_flush() {
	if (!g_thread) return;
	monitor_lock(mon());
	for (int i = 0; i < 100 && (!g_pending.empty() || g_writing); i++) monitor_wait(mon(), 10000);
	monitor_unlock(mon());
}

bool log_file_ok() { return g_file != nullptr; }

void log_file_write(const char *msg) {
	if (!g_t0) g_t0 = time_us();
	char stamp[32];
	double t = (time_us() - g_t0) / 1e6;
	snprintf(stamp, sizeof(stamp), "[%8.3f] ", t);
	monitor_lock(mon());
	// Messages may arrive in pieces; stamp only at line starts.
	static bool at_line_start = true;
	std::string out;
	for (const char *p = msg; *p; p++) {
		if (at_line_start) { out += stamp; at_line_start = false; }
		out += *p;
		if (*p == '\n') at_line_start = true;
	}
	if (g_file && g_thread) {
		if (g_pending.size() < 1024 * 1024) g_pending += out;   // the writer is far behind: drop rather than grow
		monitor_notify(mon());
	} else if (g_file) {
		fputs(out.c_str(), g_file);
		fflush(g_file);
	} else if (!g_tried && g_pending.size() < 256 * 1024) {
		g_pending += out;
	}
	monitor_unlock(mon());
}

} // namespace sys

