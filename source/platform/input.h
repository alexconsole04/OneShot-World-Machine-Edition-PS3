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
	BTN_L1 = 1u << 11, BTN_R1 = 1u << 12, BTN_R2 = 1u << 13,
};

void init();
void update();                 // call once per frame
bool held(uint32_t b);
bool pressed(uint32_t b);      // went down this frame
// Left stick, -100..100 like the game's MoveStickPos (Y down positive).
// Already dead-zoned; 0,0 when centred.
int stick_x();
int stick_y();
bool quit_requested();         // PS button > Quit Game, or host script end

} // namespace input
