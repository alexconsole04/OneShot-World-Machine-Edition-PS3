// script_parser.h - ScriptParser: the hand-matched Ruby snippets in event
// "Script" commands (355/655) and script conditionals (111 type 12).
#pragma once
#include <string>

class TileMap;
class EventRunner;

namespace script {
enum class CondResult { No, Yes, EdText };
// Returns true when the runner should stop for this frame (C# semantics).
bool HandleScript(TileMap *map, const std::string &script, EventRunner *caller, int pageId);
CondResult HandleScriptConditional(TileMap *map, const std::string &script, EventRunner *caller, int pageId);
} // namespace script
