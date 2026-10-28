// input.h - controller state mapped to the game's InputManager.Button set.
//
// Default DualShock mapping follows OneShot WME's own gamepad defaults
// (InputMapping.cs, XNA layout):  Cross=OK  Circle=Cancel  Square=Run
// Triangle=Inventory  Start=MainMenu  L2=MashText.
#pragma once
#include <stdint.h>

namespace input {

enum Button : uint32_t {
	BTN_DOWN = 1u << 0, BTN_LEFT = 1u << 1, BTN_RIGHT = 1u << 2, BTN_UP = 1u << 3,
	BTN_OK = 1u << 4, BTN_CANCEL = 1u << 5, BTN_RUN = 1u << 6, BTN_INVENTORY = 1u << 7,
	BTN_MAINMENU = 1u << 8, BTN_MASHTEXT = 1u << 9,
	BTN_SELECT = 1u << 10,  // debug overlay toggle (with L3/R3 combos later)
	BTN_L1 = 1u << 11, BTN_R1 = 1u << 12, BTN_R2 = 1u << 13, BTN_L3 = 1u << 14, BTN_R3 = 1u << 15,
};

void init();
void update();                 // call once per frame
bool held(uint32_t b);
bool pressed(uint32_t b);      // went down this frame
// The game's buttons and left stick, as the OneShot window sees them:
// InputManager.SuppressOneshotwindowInputs (window minimized, a system
// dialog up). held/pressed/auto_trigger/stick then report nothing for
// these; the _raw versions always see the pad.
const uint32_t GAME_BUTTONS = BTN_DOWN | BTN_LEFT | BTN_RIGHT | BTN_UP | BTN_OK | BTN_CANCEL | BTN_RUN |
                              BTN_INVENTORY | BTN_MAINMENU | BTN_MASHTEXT;
void suppress_game(bool on);
bool game_suppressed();
uint32_t suppressed_mask();
bool held_raw(uint32_t b);
bool pressed_raw(uint32_t b);
// Left stick, -100..100 like the game's MoveStickPos (Y down positive).
// Already dead-zoned; 0,0 when centred.
int stick_x();
int stick_y();
// Right stick, raw -100..100 (Y down positive), no dead zone: the World
// Machine desktop's cursor (InputManager.MouseStickPos) applies its own.
int rstick_x();
int rstick_y();
bool quit_requested();
// Count of system (XMB) events seen so far (PS3; 0 on the host).
int system_events();
// True while the system draws over the game (XMB open, system dialogs):
// the game pauses and stops drawing until it ends.
bool system_drawing();
// Frames a single button has been held (0 when up). InputManager.holdTime.
int held_frames(uint32_t b);
// InputManager.HoldAutoTriggerInput: true every `interval` frames once held
// for `first` frames.
bool auto_trigger(uint32_t b, int first, int interval);
bool auto_trigger_raw(uint32_t b, int first, int interval);   // ignores the game's suppression
// Backends call this at the end of update() with the new button state.
void tick_hold_counters(uint32_t cur);         // PS button > Quit Game, or host script end

} // namespace input
