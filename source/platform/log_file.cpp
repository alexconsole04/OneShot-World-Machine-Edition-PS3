// log_file.cpp - the on-disk log (oneshot_log.txt). Every line is written
// and flushed immediately so the file survives a crash or a hard power-off.
// Lines logged before the file is opened are buffered and written first.
#include "sys.h"
#include <stdio.h>
#include <string.h>
#include <string>

namespace sys {

static FILE *g_file;
static std::string g_pending;
static bool g_tried;
static volatile int g_lock;
static uint64_t g_t0;

static void lock() { while (__atomic_exchange_n(&g_lock, 1, __ATOMIC_ACQUIRE)) {} }
static void unlock() { __atomic_store_n(&g_lock, 0, __ATOMIC_RELEASE); }

void log_file_open(const char *path, const char *prev_path) {
	lock();
	if (!g_tried) {
		g_tried = true;
		remove(prev_path);
		rename(path, prev_path);
		g_file = fopen(path, "w");
		if (g_file) {
			fputs(g_pending.c_str(), g_file);
			fflush(g_file);
		}
		g_pending.clear();
		g_pending.shrink_to_fit();
	}
	unlock();
}

bool log_file_ok() { return g_file != nullptr; }

void log_file_write(const char *msg) {
	if (!g_t0) g_t0 = time_us();
	char stamp[32];
	double t = (time_us() - g_t0) / 1e6;
	snprintf(stamp, sizeof(stamp), "[%8.3f] ", t);
	lock();
	// Messages may arrive in pieces; stamp only at line starts.
	static bool at_line_start = true;
	std::string out;
	for (const char *p = msg; *p; p++) {
		if (at_line_start) { out += stamp; at_line_start = false; }
		out += *p;
		if (*p == '\n') at_line_start = true;
	}
	if (g_file) {
		fputs(out.c_str(), g_file);
		fflush(g_file);
	} else if (!g_tried && g_pending.size() < 256 * 1024) {
		g_pending += out;
	}
	unlock();
}

} // namespace sys
