// script_parser.cpp - stub: unported scripts are logged once and ignored
// (conditionals answer No). The full ScriptParser port follows the
// interpreter milestone.
#include "script_parser.h"
#include "../platform/sys.h"
#include <set>

namespace script {

static void unknown(const char *kind, const std::string &s) {
	static std::set<std::string> seen;
	if (seen.insert(s).second) sys::log("script: unported %s: %s\n", kind, s.c_str());
}

bool HandleScript(TileMap *, const std::string &s, EventRunner *, int) {
	unknown("script", s);
	return false;
}

CondResult HandleScriptConditional(TileMap *, const std::string &s, EventRunner *, int) {
	unknown("conditional", s);
	return CondResult::No;
}

} // namespace script
