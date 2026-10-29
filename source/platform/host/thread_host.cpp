// thread_host.cpp - thread.h with the C++ standard library.
#include "../thread.h"
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace sys {

struct Monitor {
	std::mutex mutex;
	std::condition_variable_any cond;
};

Monitor *monitor_create(const char *) { return new Monitor; }
void monitor_lock(Monitor *m) { m->mutex.lock(); }
void monitor_unlock(Monitor *m) { m->mutex.unlock(); }
void monitor_wait(Monitor *m, uint64_t timeout_us) {
	if (timeout_us) m->cond.wait_for(m->mutex, std::chrono::microseconds(timeout_us));
	else m->cond.wait(m->mutex);
}
void monitor_notify(Monitor *m) { m->cond.notify_all(); }

bool thread_start(void (*fn)(void *), void *arg, const char *, int, uint32_t) {
	std::thread(fn, arg).detach();
	return true;
}

} // namespace sys
