// thread_ps3.cpp - thread.h on lv2 (sys_lwmutex / sys_lwcond / PPU threads).
#include "../thread.h"
#include <string.h>
#include <sys/synchronization.h>
#include <sys/ppu_thread.h>

namespace sys {

struct Monitor {
	sys_lwmutex_t mutex;
	sys_lwcond_t cond;
};

Monitor *monitor_create(const char *name) {
	Monitor *m = new Monitor;
	memset(m, 0, sizeof(*m));
	sys_lwmutex_attribute_t ma;
	sys_lwmutex_attribute_initialize(ma);
	sys_lwmutex_attribute_name_set(ma.name, name);
	sys_lwmutex_create(&m->mutex, &ma);
	sys_lwcond_attribute_t ca;
	sys_lwcond_attribute_initialize(ca);
	sys_lwcond_attribute_name_set(ca.name, name);
	sys_lwcond_create(&m->cond, &m->mutex, &ca);
	return m;
}

void monitor_lock(Monitor *m) { sys_lwmutex_lock(&m->mutex, 0); }
void monitor_unlock(Monitor *m) { sys_lwmutex_unlock(&m->mutex); }
void monitor_wait(Monitor *m, uint64_t timeout_us) { sys_lwcond_wait(&m->cond, (usecond_t)timeout_us); }
void monitor_notify(Monitor *m) { sys_lwcond_signal_all(&m->cond); }

struct Start { void (*fn)(void *); void *arg; };

static void entry(uint64_t a) {
	Start *s = (Start *)(uintptr_t)a;
	s->fn(s->arg);
	delete s;
	sys_ppu_thread_exit(0);
}

bool thread_start(void (*fn)(void *), void *arg, const char *name, int priority) {
	Start *s = new Start{ fn, arg };
	sys_ppu_thread_t t;
	// Lower number = higher priority. The main thread runs at 1001
	// (SYS_PROCESS_PARAM); the audio thread at 900.
	int prio = priority < 0 ? 1500 : 1000;
	if (sys_ppu_thread_create(&t, entry, (uint64_t)(uintptr_t)s, prio, 64 * 1024, 0, name) != 0) {
		delete s;
		return false;
	}
	return true;
}

} // namespace sys
