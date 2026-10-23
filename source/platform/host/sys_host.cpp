// sys_host.cpp - host implementation of sys.h.
#include "../sys.h"
#include <string>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <malloc.h>
namespace sys {

int heap_used_kb() { struct mallinfo m = mallinfo(); return (int)(((size_t)m.uordblks + (size_t)m.hblkhd) / 1024); }

static char g_dir[512];

void log(const char *fmt, ...) {
	char buf[1024];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	fputs(buf, stdout);   // TTY (RPCS3 log / ps3load console) or terminal
	fflush(stdout);
	log_file_write(buf);
}

uint64_t time_us() {
	timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000u + ts.tv_nsec / 1000;
}

void init_log() {
	const char *p = getenv("OSPS3_LOG");
	std::string path = p ? p : "build-host/oneshot_log.txt";
	log_file_open(path.c_str(), (path + ".prev").c_str());
}

bool find_data_dir(const char *argv0) {
	(void)argv0;
	const char *env = getenv("OSPS3_DATA");
	snprintf(g_dir, sizeof(g_dir), "%s", env ? env : "data");
	log("data dir: %s\n", g_dir);
	return true;
}

const char *data_dir() { return g_dir; }

bool read_file(const char *rel, uint8_t **data, size_t *size) {
	char p[1024];
	snprintf(p, sizeof(p), "%s/%s", g_dir, rel);
	FILE *f = fopen(p, "rb");
	if (!f) {
		std::string real;
		if (resolve_case(g_dir, rel, real)) f = fopen(real.c_str(), "rb");
	}
	if (!f) return false;
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	uint8_t *b = (uint8_t *)malloc(n + 1);
	if (fread(b, 1, n, f) != (size_t)n) { fclose(f); free(b); return false; }
	fclose(f);
	b[n] = 0;
	*data = b;
	*size = (size_t)n;
	return true;
}

bool file_exists(const char *rel) {
	// Directory listings are cached, so repeated checks cost no file-system calls.
	std::string real;
	return resolve_case(g_dir, rel, real);
}

bool clock_24h() { return getenv("OSPS3_24H") != nullptr; }

const char *system_language() {
	const char *l = getenv("OSPS3_LANG");   // host testing
	return l ? l : "en";
}

const char *user_name() {
	const char *u = getenv("OSPS3_USER");
	if (!u) u = getenv("USER");
	return u && *u ? u : "Player";
}

void local_time(int *h, int *m, int *s, int *yday) {
	time_t t = time(nullptr);
	struct tm lt;
	localtime_r(&t, &lt);
	*h = lt.tm_hour; *m = lt.tm_min; *s = lt.tm_sec; *yday = lt.tm_yday + 1;
}

} // namespace sys
