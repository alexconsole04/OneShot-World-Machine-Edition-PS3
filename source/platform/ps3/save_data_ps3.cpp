// save_data_ps3.cpp - the PS3's save store (save_store.h): the system's saved
// data, one save named OSWM00001-SAVEDATA. It shows in the XMB's Saved Data
// Utility (where it can be copied to USB), is kept per PS3 user, and stays
// when the game is deleted. The save utility runs without any dialogs.
//
// Saves before 1.01 were files in the game's USRDIR/saves: the first time
// there is no saved data yet, they're copied in and that folder is renamed
// saves.old. If the save utility can't be used at all, the port keeps using
// that folder as before.
#include "../save_store.h"
#include "../sys.h"
#include "../thread.h"
#include "../user_files.h"
#include <cell/sysmodule.h>
#include <cell/sysutil.h>
#include <cell/sysutil_savedata.h>
#include <ctype.h>
#include <string.h>
#include <sys/memory.h>

namespace sys {
namespace save_store {

static const char DIR_NAME[] = "OSWM00001-SAVEDATA";
static const char OLD_DIR[] = "/dev_hdd0/game/OSWM00001/USRDIR/saves";

// Saved data file names are 8.3, upper case. The port's names map to these.
static const char *const NAMES[][2] = {
	{ "save.dat", "SAVE.DAT" },         { "p-settings.dat", "PSETTING.DAT" }, { "settings.conf", "SETTINGS.CNF" },
	{ "desktop.dat", "DESKTOP.DAT" },   { "fs.dat", "FS.DAT" },               { "language.dat", "LANGUAGE.DAT" },
	{ "map_entries.txt", "MAPCACHE.TXT" },
};

static std::string file_name(const std::string &name) {
	for (auto &n : NAMES)
		if (name == n[0]) return n[1];
	// Not in the table (a new file): A-Z 0-9 _ only, 8.3.
	std::string base, ext;
	bool inExt = false;
	for (char c : name) {
		if (c == '.') { inExt = true; continue; }
		if (c >= 'a' && c <= 'z') c -= 32;
		if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')) continue;
		std::string &part = inExt ? ext : base;
		if (part.size() < (inExt ? 3u : 8u)) part += c;
	}
	return ext.empty() ? base : base + "." + ext;
}

static std::string port_name(const char *file) {
	for (auto &n : NAMES)
		if (strcmp(file, n[1]) == 0) return n[0];
	std::string s(file);
	for (auto &c : s) c = (char)tolower((unsigned char)c);
	return s;
}

static bool g_useFolder;   // the save utility failed: plain files in OLD_DIR instead
static sys_memory_container_t g_container = SYS_MEMORY_CONTAINER_ID_INVALID;
static bool g_containerMade;

// The save utility's working buffer (its file list).
static const unsigned FILE_LIST_MAX = 64;
static uint8_t g_listBuf[FILE_LIST_MAX * sizeof(CellSaveDataFileStat) + 4096] __attribute__((aligned(64)));

static CellSaveDataSetBuf set_buf() {
	CellSaveDataSetBuf b;
	memset(&b, 0, sizeof(b));
	b.dirListMax = 0;
	b.fileListMax = FILE_LIST_MAX;
	b.bufSize = sizeof(g_listBuf);
	b.buf = g_listBuf;
	return b;
}

// ------------------------------------------------------------------ loading
static struct {
	Files *out;
	std::vector<CellSaveDataFileStat> list;
	size_t next;
	std::vector<char> buf;
	bool isNew;
} L;

static void load_stat(CellSaveDataCBResult *r, CellSaveDataStatGet *get, CellSaveDataStatSet *set) {
	set->setParam = nullptr;   // leave PARAM.SFO as it is
	set->reCreateMode = CELL_SAVEDATA_RECREATE_NO;
	set->indicator = nullptr;
	L.isNew = get->isNewData != 0;
	L.out->clear();   // (a second attempt starts over)
	L.list.clear();
	L.next = 0;
	for (unsigned i = 0; i < get->fileListNum; i++) {
		const CellSaveDataFileStat &f = get->fileList[i];
		if (f.fileType == CELL_SAVEDATA_FILETYPE_NORMALFILE || f.fileType == CELL_SAVEDATA_FILETYPE_SECUREFILE)
			L.list.push_back(f);
	}
	r->result = L.isNew || L.list.empty() ? CELL_SAVEDATA_CBRESULT_OK_LAST : CELL_SAVEDATA_CBRESULT_OK_NEXT;
}

static void load_file(CellSaveDataCBResult *r, CellSaveDataFileGet *get, CellSaveDataFileSet *set) {
	if (L.next > 0) {   // the file asked for last time has been read
		const CellSaveDataFileStat &f = L.list[L.next - 1];
		(*L.out)[port_name(f.fileName)].assign(L.buf.data(), get->excSize);
	}
	if (L.next >= L.list.size()) { r->result = CELL_SAVEDATA_CBRESULT_OK_LAST; return; }
	CellSaveDataFileStat &f = L.list[L.next++];
	L.buf.assign((size_t)f.st_size + 1, 0);
	memset(set, 0, sizeof(*set));
	set->fileOperation = CELL_SAVEDATA_FILEOP_READ;
	set->fileType = f.fileType;
	set->fileName = f.fileName;
	set->fileOffset = 0;
	set->fileSize = (unsigned)f.st_size;
	set->fileBufSize = (unsigned)L.buf.size();
	set->fileBuf = L.buf.data();
	r->result = CELL_SAVEDATA_CBRESULT_OK_NEXT;
}

// ------------------------------------------------------------------ saving
struct Op { unsigned operation, type; std::string file; const std::string *data; };

static struct {
	std::vector<Op> writes;             // this commit's files
	std::vector<Op> ops;                // and the deletes and icon the stat callback adds
	std::vector<std::string> deletes;   // port names, deleted only if they exist
	size_t next;
	std::vector<uint8_t> icon;          // ICON0.PNG for the XMB, written when missing
	bool iconTried;
} S;

static void save_stat(CellSaveDataCBResult *r, CellSaveDataStatGet *get, CellSaveDataStatSet *set) {
	CellSaveDataSystemFileParam *p = &get->getParam;
	strncpy(p->title, "OneShot: World Machine Edition", sizeof(p->title) - 1);
	strncpy(p->subTitle, "PS3 port", sizeof(p->subTitle) - 1);
	strncpy(p->detail, "Your game, the World Machine desktop and your settings.", sizeof(p->detail) - 1);
	p->attribute = CELL_SAVEDATA_ATTR_NORMAL;
	set->setParam = p;
	set->reCreateMode = CELL_SAVEDATA_RECREATE_NO;
	set->indicator = nullptr;
	S.ops = S.writes;
	S.next = 0;
	bool hasIcon = false;
	for (unsigned i = 0; i < get->fileListNum; i++) {
		const CellSaveDataFileStat &f = get->fileList[i];
		if (f.fileType == CELL_SAVEDATA_FILETYPE_CONTENT_ICON0) hasIcon = true;
		for (auto &d : S.deletes)
			if (f.fileType == CELL_SAVEDATA_FILETYPE_NORMALFILE && file_name(d) == f.fileName)
				S.ops.push_back(Op{ CELL_SAVEDATA_FILEOP_DELETE, CELL_SAVEDATA_FILETYPE_NORMALFILE, f.fileName, nullptr });
	}
	if (!hasIcon && !S.icon.empty()) {
		static std::string icon;
		icon.assign(S.icon.begin(), S.icon.end());
		S.ops.push_back(Op{ CELL_SAVEDATA_FILEOP_WRITE, CELL_SAVEDATA_FILETYPE_CONTENT_ICON0, "ICON0.PNG", &icon });
	}
	r->result = S.ops.empty() ? CELL_SAVEDATA_CBRESULT_OK_LAST : CELL_SAVEDATA_CBRESULT_OK_NEXT;
}

static void save_file(CellSaveDataCBResult *r, CellSaveDataFileGet *, CellSaveDataFileSet *set) {
	if (S.next >= S.ops.size()) { r->result = CELL_SAVEDATA_CBRESULT_OK_LAST; return; }
	Op &op = S.ops[S.next++];
	memset(set, 0, sizeof(*set));
	set->fileOperation = op.operation;
	set->fileType = op.type;
	set->fileName = (char *)op.file.c_str();
	if (op.data) {
		set->fileSize = (unsigned)op.data->size();
		set->fileBufSize = (unsigned)op.data->size();
		set->fileBuf = (void *)op.data->data();
	}
	r->result = CELL_SAVEDATA_CBRESULT_OK_NEXT;
}

// ------------------------------------------------------------------ calling the utility
static bool module_loaded() {
	static int state;   // 0 not tried, 1 loaded, -1 failed
	if (!state) {
		int rc = cellSysmoduleLoadModule(CELL_SYSMODULE_SYSUTIL_SAVEDATA);
		state = rc == 0 ? 1 : -1;
		if (rc) log("save: the save data module didn't load (0x%08x)\n", (unsigned)rc);
	}
	return state > 0;
}

// Runs a save utility call; if it fails for want of memory, once more with
// a memory container of its own (some firmware wants one).
template <typename F> static int run(const char *what, F call) {
	int rc = call(g_container);
	if (rc != CELL_SAVEDATA_RET_OK && rc != (int)CELL_SAVEDATA_ERROR_NODATA && !g_containerMade) {
		g_containerMade = true;
		sys_memory_container_t c;
		if (sys_memory_container_create(&c, 4 * 1024 * 1024) == 0) {
			log("save: %s failed (0x%08x); again with a memory container\n", what, (unsigned)rc);
			g_container = c;
			rc = call(g_container);
		}
	}
	return rc;
}

static int do_load(Files &files) {
	L.out = &files;
	L.isNew = false;
	CellSaveDataSetBuf buf = set_buf();
	return run("loading", [&](sys_memory_container_t c) {
		return cellSaveDataAutoLoad2(CELL_SAVEDATA_VERSION_CURRENT, DIR_NAME, CELL_SAVEDATA_ERRDIALOG_NONE, &buf, load_stat,
		                             load_file, c, nullptr);
	});
}

static int do_save(const Files &writes, const std::vector<std::string> &deletes) {
	if (!S.iconTried) {
		S.iconTried = true;
		std::string usr = data_dir();   // .../USRDIR: the icon is next to it
		read_abs(usr.substr(0, usr.find_last_of('/')) + "/ICON0.PNG", S.icon);
	}
	S.writes.clear();
	S.deletes = deletes;
	for (auto &w : writes)
		S.writes.push_back(Op{ CELL_SAVEDATA_FILEOP_WRITE, CELL_SAVEDATA_FILETYPE_NORMALFILE, file_name(w.first), &w.second });
	CellSaveDataSetBuf buf = set_buf();
	return run("saving", [&](sys_memory_container_t c) {
		return cellSaveDataAutoSave2(CELL_SAVEDATA_VERSION_CURRENT, DIR_NAME, CELL_SAVEDATA_ERRDIALOG_NONE, &buf, save_stat,
		                             save_file, c, nullptr);
	});
}

// ------------------------------------------------------------------ save_store
// Loading happens on a thread of its own while the game thread keeps the
// system callbacks serviced (the save utility may need them).
static struct {
	Monitor *mon;
	Files *files;
	bool done, ok;
} J;

static void load_job(void *) {
	Files &files = *J.files;
	bool ok = false;
	if (module_loaded()) {
		uint64_t t0 = time_us();
		int rc = do_load(files);
		bool empty = rc == (int)CELL_SAVEDATA_ERROR_NODATA || (rc == CELL_SAVEDATA_RET_OK && files.empty());
		ok = rc == CELL_SAVEDATA_RET_OK || empty;
		log("save: saved data %s: 0x%08x, %d files (%.0f ms)\n", DIR_NAME, (unsigned)rc, (int)files.size(),
		    (time_us() - t0) / 1000.0);
		if (ok && empty) {   // the saves from before 1.01, if any, move in
			Files old;
			folder_load_all(OLD_DIR, old);
			if (!old.empty()) {
				int src = do_save(old, {});
				log("save: copied %d files from %s into the saved data: 0x%08x\n", (int)old.size(), OLD_DIR, (unsigned)src);
				if (src == CELL_SAVEDATA_RET_OK) {
					files = old;
					std::string kept = std::string(OLD_DIR) + ".old";
					if (!rename_abs(OLD_DIR, kept)) log("save: couldn't rename %s\n", OLD_DIR);
				} else {
					ok = false;   // keep using the old folder rather than lose those saves
				}
			}
		}
	}
	if (!ok) {
		g_useFolder = true;
		files.clear();
		folder_load_all(OLD_DIR, files);
		log("save: using the folder %s\n", OLD_DIR);
	}
	MonitorLock l(J.mon);
	J.ok = true;
	J.done = true;
	monitor_notify(J.mon);
}

bool load_all(Files &files) {
	J.mon = monitor_create("save load");
	J.files = &files;
	J.done = false;
	if (!J.mon || !thread_start(load_job, nullptr, "save load", 0, 256 * 1024)) {
		load_job(nullptr);   // no thread: here, and hope no callback is needed
		return J.ok;
	}
	for (;;) {
		{
			MonitorLock l(J.mon);
			if (J.done) break;
			monitor_wait(J.mon, 5000);
		}
		cellSysutilCheckCallback();
	}
	return J.ok;
}

bool commit(const Files &writes, const std::vector<std::string> &deletes) {
	if (g_useFolder) return folder_commit(OLD_DIR, writes, deletes);
	int rc = do_save(writes, deletes);
	if (rc != CELL_SAVEDATA_RET_OK) log("save: saving to the saved data failed: 0x%08x\n", (unsigned)rc);
	return rc == CELL_SAVEDATA_RET_OK;
}

void pump() { cellSysutilCheckCallback(); }

const char *where() { return g_useFolder ? OLD_DIR : "the system's saved data (" "OSWM00001-SAVEDATA" ")"; }

} // namespace save_store
} // namespace sys
