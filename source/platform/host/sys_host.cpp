// sys_host.cpp - host implementation of sys.h.
#include "../sys.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

namespace sys {

static char g_dir[512];

void log(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	fflush(stdout);
}

uint64_t time_us() {
	timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000u + ts.tv_nsec / 1000;
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
	char p[1024];
	snprintf(p, sizeof(p), "%s/%s", g_dir, rel);
	FILE *f = fopen(p, "rb");
	if (!f) return false;
	fclose(f);
	return true;
}

} // namespace sys
