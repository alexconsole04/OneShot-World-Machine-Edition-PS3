// sys_ps3.cpp - PS3 implementation of sys.h.
#include "../sys.h"
#include <cell/sysutil_sysparam.h>
#include <string>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/systime.h>
#include <cell/sysmodule.h>
#include <cell/sysutil_userinfo.h>
#include <cell/rtc.h>
#include <cell/cell_fs.h>
#include <sysutil/sysutil_common.h>

#define TITLE_ID "OSWM00001"

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
	log_file_write(buf);
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

void init_log() {
	log_file_open("/dev_hdd0/game/" TITLE_ID "/USRDIR/oneshot_log.txt",
	              "/dev_hdd0/game/" TITLE_ID "/USRDIR/oneshot_log_prev.txt");
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

// One cellFsRead for the whole file: the C library's stdio reads in small
// pieces, each a separate system call (a 2 MB music file took ~160 ms).
static bool read_whole(const char *path, uint8_t **data, size_t *size) {
	int fd;
	if (cellFsOpen(path, CELL_FS_O_RDONLY, &fd, nullptr, 0) != CELL_FS_SUCCEEDED) return false;
	CellFsStat st;
	if (cellFsFstat(fd, &st) != CELL_FS_SUCCEEDED) { cellFsClose(fd); return false; }
	size_t n = (size_t)st.st_size;
	uint8_t *b = (uint8_t *)malloc(n + 1);
	uint64_t got = 0;
	bool ok = b != nullptr;
	while (ok && got < n) {
		uint64_t r = 0;
		if (cellFsRead(fd, b + got, n - got, &r) != CELL_FS_SUCCEEDED || r == 0) ok = false;
		got += r;
	}
	cellFsClose(fd);
	if (!ok) { free(b); return false; }
	b[n] = 0;
	*data = b;
	*size = n;
	return true;
}

bool read_file(const char *rel, uint8_t **data, size_t *size) {
	uint64_t t0 = time_us();
	char p[1024];
	snprintf(p, sizeof(p), "%s/%s", g_dir, rel);
	bool ok = read_whole(p, data, size);
	if (!ok) {
		std::string real;
		if (resolve_case(g_dir, rel, real)) ok = read_whole(real.c_str(), data, size);
	}
	uint64_t ms = (time_us() - t0) / 1000;
	if (ms >= 30) log("file: %s took %u ms (%u KB)\n", rel, (unsigned)ms, ok ? (unsigned)(*size / 1024) : 0u);
	return ok;
}

bool file_exists(const char *rel) {
	// Directory listings are cached, so repeated checks cost no file-system calls.
	std::string real;
	return resolve_case(g_dir, rel, real);
}

bool clock_24h() {
	int v = 0;
	if (cellSysutilGetSystemParamInt(CELL_SYSUTIL_SYSTEMPARAM_ID_TIME_FORMAT, &v) != 0) return false;
	return v == CELL_SYSUTIL_TIME_FMT_CLOCK24;
}

const char *system_language() {
	// The console's language, as a game language code (GetDefaultLanguageCode).
	int v = -1;
	if (cellSysutilGetSystemParamInt(CELL_SYSUTIL_SYSTEMPARAM_ID_LANG, &v) != 0) return "en";
	switch (v) {
	case CELL_SYSUTIL_LANG_JAPANESE: return "ja";
	case CELL_SYSUTIL_LANG_FRENCH: return "fr";
	case CELL_SYSUTIL_LANG_SPANISH: return "es";
	case CELL_SYSUTIL_LANG_ITALIAN: return "it";
	case CELL_SYSUTIL_LANG_PORTUGUESE_PT:
	case CELL_SYSUTIL_LANG_PORTUGUESE_BR: return "pt_br";
	case CELL_SYSUTIL_LANG_RUSSIAN: return "ru";
	case CELL_SYSUTIL_LANG_KOREAN: return "ko";
	case CELL_SYSUTIL_LANG_CHINESE_T: return "zh_cht";
	case CELL_SYSUTIL_LANG_CHINESE_S: return "zh_cn";
	default: return "en";
	}
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
