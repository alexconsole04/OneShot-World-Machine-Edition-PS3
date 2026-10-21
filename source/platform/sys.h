// sys.h - logging, timing, files, data-folder discovery.
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace sys {

void log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

uint64_t time_us();   // monotonic microseconds

// Locate the folder holding the user's own "content" and "gamedata".
bool find_data_dir(const char *argv0);
const char *data_dir();

// Read <data_dir>/<rel> fully. Caller frees with free(). Adds a trailing 0
// byte (not counted in size) so text files can be parsed in place.
bool read_file(const char *rel, uint8_t **data, size_t *size);
bool file_exists(const char *rel);

// Case-insensitive lookup of root/rel; out = the real full path.
}
#include <string>
#include <vector>
namespace sys {
bool resolve_case(const char *root, const char *rel, std::string &out);

// Opens the platform's log file (call first thing in main).
void init_log();
// On-disk log: open once (moves the previous log to prev_path).
void log_file_open(const char *path, const char *prev_path);
void log_file_write(const char *msg);
bool log_file_ok();

// Names of the entries in <data_dir>/<rel> (case as on disk).
bool list_dir(const char *rel, std::vector<std::string> &out);

// The console's / OS's user name (OneShot addresses the player by it).
const char *user_name();
// Local wall-clock time (DateTime.Now): fills hour/min/sec and day of year (1..366).
void local_time(int *hour, int *minute, int *second, int *day_of_year);

} // namespace sys
