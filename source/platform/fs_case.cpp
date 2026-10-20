// fs_case.cpp - case-insensitive path resolution. The game's data names
// files with mixed case ("DOORS2.xnb") but refers to them in lower case;
// Windows didn't care, the PS3's file system (and Linux) do.
#include "sys.h"
#include <ctype.h>
#include <dirent.h>
#include <string>
#include <unordered_map>

namespace sys {

static std::string lower(std::string s) {
	for (auto &c : s) c = (char)tolower((unsigned char)c);
	return s;
}

// directory (real path) -> lower-case name -> real name
static std::unordered_map<std::string, std::unordered_map<std::string, std::string>> listings;

static const std::unordered_map<std::string, std::string> &listing(const std::string &dir) {
	auto it = listings.find(dir);
	if (it != listings.end()) return it->second;
	auto &m = listings[dir];
	if (DIR *d = opendir(dir.c_str())) {
		while (struct dirent *e = readdir(d)) {
			std::string n = e->d_name;
			if (n == "." || n == "..") continue;
			m.emplace(lower(n), n);
		}
		closedir(d);
	}
	return m;
}

bool resolve_case(const char *root, const char *rel, std::string &out) {
	std::string cur = root;
	std::string r = rel;
	size_t p = 0;
	while (p <= r.size()) {
		size_t q = r.find('/', p);
		std::string part = r.substr(p, q == std::string::npos ? std::string::npos : q - p);
		if (!part.empty()) {
			const auto &m = listing(cur);
			auto it = m.find(lower(part));
			if (it == m.end()) return false;
			cur += "/" + it->second;
		}
		if (q == std::string::npos) break;
		p = q + 1;
	}
	out = cur;
	return true;
}

} // namespace sys
