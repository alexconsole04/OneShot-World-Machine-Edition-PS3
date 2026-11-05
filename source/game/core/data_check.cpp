// data_check.cpp - see data_check.h. One directory listing per folder
// (about 140), not one check per file (about 4600): a few hundred
// milliseconds on the PS3's hard drive, off the game thread.
#include "data_check.h"
#include "../../platform/sys.h"
#include "../../platform/thread.h"
#include "../../platform/user_files.h"
#include <map>
#include <set>
#include <string>
#include <vector>

namespace data_check {

static volatile bool g_done;
static int g_missing, g_checked;

static void check(void *) {
	uint64_t t0 = sys::time_us();
	std::vector<uint8_t> list;
	if (!sys::read_abs(sys::port_dir() + "/game_files.txt", list)) {
		sys::log("data check: no game_files.txt in %s, skipped\n", sys::port_dir().c_str());
		g_done = true;
		return;
	}
	// folder -> the file names expected in it
	std::map<std::string, std::vector<std::string>> expected;
	std::string all(list.begin(), list.end());
	for (size_t p = 0; p < all.size();) {
		size_t e = all.find('\n', p);
		if (e == std::string::npos) e = all.size();
		std::string line = all.substr(p, e - p);
		p = e + 1;
		if (!line.empty() && line.back() == '\r') line.pop_back();
		if (line.empty() || line[0] == '#') continue;
		size_t slash = line.find_last_of('/');
		if (slash == std::string::npos) continue;
		expected[line.substr(0, slash)].push_back(line.substr(slash + 1));
	}
	int missing = 0, checked = 0;
	const int LOG_MAX = 100;   // the rest is only counted
	for (auto &dir : expected) {
		std::vector<std::string> names;
		sys::list_dir(dir.first.c_str(), names);
		std::set<std::string> present(names.begin(), names.end());
		for (auto &name : dir.second) {
			checked++;
			if (present.count(name)) continue;
			if (++missing <= LOG_MAX) sys::log("data check: MISSING %s/%s\n", dir.first.c_str(), name.c_str());
		}
	}
	if (missing > LOG_MAX) sys::log("data check: ... and %d more missing\n", missing - LOG_MAX);
	sys::log("data check: %d of %d game files present%s (%.0f ms)\n", checked - missing, checked,
	         missing ? "" : ", none missing", (sys::time_us() - t0) / 1000.0);
	g_missing = missing;
	g_checked = checked;
	__sync_synchronize();   // the counts are visible before g_done
	g_done = true;
}

void start() {
	if (!sys::thread_start(check, nullptr, "data check", -1)) check(nullptr);
}

bool done() { return g_done; }
int missing_count() { return g_done ? g_missing : 0; }
int checked_count() { return g_checked; }

} // namespace data_check
