// json_file.h - reading the game's JSON files, which use trailing commas
// (Newtonsoft accepts them; cJSON doesn't).
#pragma once
#include "cJSON.h"

// The parsed file, or nullptr if it's missing or unreadable. Free with cJSON_Delete.
cJSON *parse_json_file(const char *rel);
