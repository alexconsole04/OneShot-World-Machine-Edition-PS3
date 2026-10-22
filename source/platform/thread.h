// thread.h - minimal threads and locks for background work (log writing,
// file loading). PS3: PPU threads with lightweight mutexes/conditions (the
// waiting side sleeps in the kernel, no busy-waiting). Host: std::thread.
#pragma once
#include <stdint.h>

namespace sys {

// A mutex with one condition variable attached.
struct Monitor;
Monitor *monitor_create(const char *name);
void monitor_lock(Monitor *m);
void monitor_unlock(Monitor *m);
// Call with the lock held: releases it while sleeping, re-takes it before
// returning. Returns after a notify or after timeout_us (0 = no timeout).
void monitor_wait(Monitor *m, uint64_t timeout_us);
void monitor_notify(Monitor *m);   // wakes all waiters

struct MonitorLock {
	Monitor *m;
	explicit MonitorLock(Monitor *mm) : m(mm) { monitor_lock(m); }
	~MonitorLock() { monitor_unlock(m); }
};

// Runs fn(arg) on a new thread. priority: 0 = normal, -1 = lower than the
// game (background work). False if the thread couldn't be created.
bool thread_start(void (*fn)(void *), void *arg, const char *name, int priority);

} // namespace sys
