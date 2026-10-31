// loader.h - reads files on a background thread so the game thread never
// waits on the hard drive (music files are megabytes).
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace sys {

struct LoadJob;

// Queue <data_dir>/<rel> for reading. Never returns null. Low-priority reads
// (prefetching) wait until no other read is queued.
LoadJob *load_async(const char *rel, bool lowPriority = false);
// True once the read has finished (successfully or not).
bool load_done(LoadJob *j);
// Blocks until the read has finished (a low-priority read goes first).
void load_wait(LoadJob *j);
// After load_done: takes the data (caller frees with free()) and releases
// the job. False (and nothing to free) if the file couldn't be read.
bool load_take(LoadJob *j, uint8_t **data, size_t *size);
// Releases a job whose result is no longer wanted (finished or not).
void load_cancel(LoadJob *j);

} // namespace sys
