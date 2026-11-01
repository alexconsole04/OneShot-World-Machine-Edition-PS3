// test_hooks.h - environment switches for host test runs (tools/test/flow.sh).
// On the PS3 there is no environment, so every one of these does nothing.
// The list of switches is in docs/DEV_NOTES.md.
#pragma once
#include <stdint.h>

struct GameState;

namespace test_hooks {

// OSPS3_SEED: a fixed random seed; returns fallback when unset.
uint32_t seed(uint32_t fallback);

// OSPS3_MAP: the map to start on, or -1 for a normal start.
int start_map();
// OSPS3_SPAWN="x,y": overrides the start position on that map.
void start_position(float &x, float &y);
// OSPS3_SKIP_BOOT / OSPS3_MAP: start without the boot sequence.
bool skip_boot();
// OSPS3_LAUNCH: open the OneShot window straight away.
bool launch();

// OSPS3_FLAGS, OSPS3_VARS, OSPS3_ITEMS, OSPS3_TRAVEL, OSPS3_FOLLOWERS.
void apply_game_state(GameState &state);
// OSPS3_TEXTS="a|b": runs those Show Text commands on the current map.
void run_texts();

// OSPS3_SHOTS="frame:file.png,...": saves a screenshot on those frames.
void screenshots(int frame);

// OSPS3_AUDIO_LOG, OSPS3_OVERLAY, OSPS3_TEXVIEW=<index>.
bool audio_log();
bool overlay();
bool texture_viewer(int &index);

} // namespace test_hooks
