// audio_out_ps3.cpp - cellAudio output. One stereo float port with 8
// blocks of 256 frames; a PPU thread sleeps on the mixer's notify event
// queue (no busy-waiting) and keeps the port LATENCY blocks ahead of the
// hardware read position. The game thread never waits on this thread.
#include "../audio_out.h"
#include "../sys.h"
#include "../../engine/mixer.h"

#include <cell/audio.h>
#include <cell/sysmodule.h>
#include <sys/event.h>
#include <sys/ppu_thread.h>
#include <string.h>

namespace audio_out {

static const int LATENCY = 4;   // blocks queued ahead (~21 ms at 48 kHz)
static uint32_t port_num;
static CellAudioPortConfig cfg;
static sys_event_queue_t queue;
static sys_ipc_key_t key;
static sys_ppu_thread_t thread;
static volatile bool running;
static uint32_t write_block;      // next ring slot to fill (0..nBlock-1)
static volatile uint32_t blocks_mixed;

static void fill_block(uint32_t slot) {
	float *dst = (float *)(uintptr_t)cfg.portAddr + slot * CELL_AUDIO_BLOCK_SAMPLES * 2;
	mixer::render(dst, CELL_AUDIO_BLOCK_SAMPLES);
	blocks_mixed++;
}

// readIndexAddr holds the ring slot the hardware is playing (0..nBlock-1),
// not a running count. Keep the slots after it filled, LATENCY deep.
static void top_up() {
	const uint32_t n = (uint32_t)cfg.nBlock;
	uint32_t read = (uint32_t)(*(volatile uint64_t *)(uintptr_t)cfg.readIndexAddr % n);
	uint32_t ahead = (write_block + n - read) % n;   // slots queued after the playing one, +1
	if (ahead == 0 || ahead > (uint32_t)LATENCY + 1) write_block = (read + 1) % n;   // fell behind: restart just after it
	while ((write_block + n - read) % n <= (uint32_t)LATENCY) {
		fill_block(write_block);
		write_block = (write_block + 1) % n;
	}
}

static void thread_main(uint64_t) {
	while (running) {
		sys_event_t ev;
		// Woken by the mixer every block (~5.3 ms). On a timeout or error
		// still top up, so a lost notification never silences the game.
		sys_event_queue_receive(queue, &ev, 20 * 1000);
		top_up();
	}
	sys_ppu_thread_exit(0);
}

unsigned mixed_blocks() { return blocks_mixed; }

bool start(int rate) {
	(void)rate;   // cellAudio always mixes at 48 kHz
	int r = cellSysmoduleLoadModule(CELL_SYSMODULE_AUDIO);
	if (r != 0) sys::log("audio: load audio module returned 0x%08x (continuing)\n", r);
	r = cellAudioInit();
	if (r != 0 && (uint32_t)r != CELL_AUDIO_ERROR_ALREADY_INIT) { sys::log("audio: cellAudioInit failed 0x%08x\n", r); return false; }
	CellAudioPortParam p;
	memset(&p, 0, sizeof(p));
	p.nChannel = CELL_AUDIO_PORT_2CH;
	p.nBlock = CELL_AUDIO_BLOCK_8;
	p.attr = 0;
	p.level = 1.0f;
	if ((r = cellAudioPortOpen(&p, &port_num)) != 0) { sys::log("audio: cellAudioPortOpen failed 0x%08x\n", r); return false; }
	if ((r = cellAudioGetPortConfig(port_num, &cfg)) != 0) { sys::log("audio: cellAudioGetPortConfig failed 0x%08x\n", r); return false; }
	memset((void *)(uintptr_t)cfg.portAddr, 0, cfg.portSize);
	if ((r = cellAudioCreateNotifyEventQueue(&queue, &key)) != 0) { sys::log("audio: event queue failed 0x%08x\n", r); return false; }
	if ((r = cellAudioSetNotifyEventQueue(key)) != 0) { sys::log("audio: set event queue failed 0x%08x\n", r); return false; }
	for (int i = 1; i <= LATENCY; i++) fill_block((uint32_t)i % (uint32_t)cfg.nBlock);
	write_block = (uint32_t)(LATENCY + 1) % (uint32_t)cfg.nBlock;
	if ((r = cellAudioPortStart(port_num)) != 0) { sys::log("audio: cellAudioPortStart failed 0x%08x\n", r); return false; }
	running = true;
	// Priority above the game thread (1001) so mixing preempts game logic.
	if ((r = sys_ppu_thread_create(&thread, thread_main, 0, 900, 64 * 1024, SYS_PPU_THREAD_CREATE_JOINABLE, "audio")) != 0) {
		sys::log("audio: thread create failed 0x%08x\n", r);
		running = false;
		return false;
	}
	sys::log("audio: port %u, %u blocks, latency %d blocks\n", port_num, (unsigned)cfg.nBlock, LATENCY);
	return true;
}

void stop() {
	if (!running) return;
	running = false;
	uint64_t ret;
	sys_ppu_thread_join(thread, &ret);
	cellAudioPortStop(port_num);
	cellAudioRemoveNotifyEventQueue(key);
	sys_event_queue_destroy(queue, 0);
	cellAudioPortClose(port_num);
	cellAudioQuit();
}

void pump() {}

} // namespace audio_out
