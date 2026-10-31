#include "loader.h"
#include "sys.h"
#include "thread.h"
#include <stdlib.h>
#include <deque>
#include <string>

namespace sys {

struct LoadJob {
	std::string rel;
	uint8_t *data = nullptr;
	size_t size = 0;
	bool ok = false, done = false, cancelled = false;
};

static Monitor *g_mon;
static std::deque<LoadJob *> g_queue, g_low;   // g_low: prefetches, read when g_queue is empty
static bool g_started, g_thread_ok;

static void worker(void *) {
	for (;;) {
		monitor_lock(g_mon);
		while (g_queue.empty() && g_low.empty()) monitor_wait(g_mon, 0);
		std::deque<LoadJob *> &q = g_queue.empty() ? g_low : g_queue;
		LoadJob *j = q.front();
		q.pop_front();
		bool skip = j->cancelled;
		monitor_unlock(g_mon);
		uint8_t *d = nullptr;
		size_t n = 0;
		bool ok = !skip && read_file(j->rel.c_str(), &d, &n);
		monitor_lock(g_mon);
		if (j->cancelled) {   // nobody wants it any more
			free(d);
			delete j;
		} else {
			j->data = d;
			j->size = n;
			j->ok = ok;
			j->done = true;
			monitor_notify(g_mon);   // load_wait
		}
		monitor_unlock(g_mon);
	}
}

LoadJob *load_async(const char *rel, bool lowPriority) {
	if (!g_started) {
		g_started = true;
		g_mon = monitor_create("loader");
		g_thread_ok = thread_start(worker, nullptr, "file loader", -1);
		if (!g_thread_ok) log("loader: thread create failed, reading on the game thread\n");
	}
	LoadJob *j = new LoadJob;
	j->rel = rel;
	if (!g_thread_ok) {   // fallback: read now
		j->ok = read_file(rel, &j->data, &j->size);
		j->done = true;
		return j;
	}
	monitor_lock(g_mon);
	(lowPriority ? g_low : g_queue).push_back(j);
	monitor_notify(g_mon);
	monitor_unlock(g_mon);
	return j;
}

bool load_done(LoadJob *j) {
	if (!g_thread_ok) return true;
	MonitorLock l(g_mon);
	return j->done;
}

void load_wait(LoadJob *j) {
	if (!g_thread_ok) return;
	MonitorLock l(g_mon);
	for (auto it = g_low.begin(); it != g_low.end(); ++it)
		if (*it == j) {   // someone needs it now: to the front of the line
			g_low.erase(it);
			g_queue.push_front(j);
			break;
		}
	while (!j->done) monitor_wait(g_mon, 5000);
}

bool load_take(LoadJob *j, uint8_t **data, size_t *size) {
	bool ok = j->ok;
	if (ok) { *data = j->data; *size = j->size; }
	delete j;
	return ok;
}

void load_cancel(LoadJob *j) {
	if (!j) return;
	if (!g_thread_ok) { free(j->data); delete j; return; }
	MonitorLock l(g_mon);
	if (j->done) { free(j->data); delete j; }
	else j->cancelled = true;   // the worker frees it (queued or reading)
}

} // namespace sys
