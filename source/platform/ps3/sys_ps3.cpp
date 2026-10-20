// sys_ps3.cpp - PS3 implementation of sys.h.
#include "../sys.h"
#include <string>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/systime.h>
#include <cell/sysmodule.h>
#include <cell/sysutil_userinfo.h>
#include <cell/rtc.h>
#include <sysutil/sysutil_common.h>

#define TITLE_ID "OSWM00001"

namespace sys {

static char g_dir[512];

void log(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	vprintf(fmt, ap);   // TTY -> RPCS3 log / ps3load console
	va_end(ap);
}

uint64_t time_us() {
	u64 sec, nsec;
	sysGetCurrentTime(&sec, &nsec);
	return sec * 1000000ull + nsec / 1000ull;
}

static bool exists(const char *dir, const char *rel) {
	char p[768];
	snprintf(p, sizeof(p), "%s/%s", dir, rel);
	FILE *f = fopen(p, "rb");
	if (!f) return false;
	fclose(f);
	return true;
}

bool find_data_dir(const char *argv0) {
	char cand[4][512];
	int n = 0;
	if (argv0 && argv0[0] == '/') {
		snprintf(cand[n], sizeof(cand[n]), "%s", argv0);
		char *slash = strrchr(cand[n], '/');
		if (slash) { *slash = 0; n++; }
	}
	snprintf(cand[n++], 512, "/dev_hdd0/game/%s/USRDIR", TITLE_ID);
	snprintf(cand[n++], 512, "/dev_usb000/oneshot");
	snprintf(cand[n++], 512, "/dev_hdd0/oneshot");
	for (int i = 0; i < n; i++) {
		if (exists(cand[i], "gamedata/oneshot_tilesets.json")) {
			snprintf(g_dir, sizeof(g_dir), "%s", cand[i]);
			log("data dir: %s\n", g_dir);
			return true;
		}
		log("no game data in %s\n", cand[i]);
	}
	return false;
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
	if (!b || fread(b, 1, n, f) != (size_t)n) { fclose(f); free(b); return false; }
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

const char *user_name() {
	static char name[64];
	static bool tried;
	if (!tried) {
		tried = true;
		if (cellSysmoduleLoadModule(CELL_SYSMODULE_SYSUTIL_USERINFO) == 0) {
			CellUserInfoUserStat st;
			memset(&st, 0, sizeof(st));
			if (cellUserInfoGetStat(CELL_SYSUTIL_USERID_CURRENT, &st) == 0) {
				snprintf(name, sizeof(name), "%.63s", st.name);
				log("user name: %s\n", name);
			}
		}
		if (!name[0]) strcpy(name, "Player");
	}
	return name;
}

void local_time(int *h, int *m, int *s, int *yday) {
	CellRtcDateTime t;
	memset(&t, 0, sizeof(t));
	cellRtcGetCurrentClockLocalTime(&t);
	*h = t.hour; *m = t.minute; *s = t.second;
	int d = t.day;
	for (int mo = 1; mo < t.month; mo++) d += cellRtcGetDaysInMonth(t.year, mo);
	*yday = d;
}

} // namespace sys
