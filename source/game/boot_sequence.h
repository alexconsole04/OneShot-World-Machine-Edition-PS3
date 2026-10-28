// boot_sequence.h - BootManager: the World Machine's start-up sequence.
//
//   Felix -> Future Cat logo -> KOMODO logo -> language select (first
//   launch only) -> autosave notice -> BIOS console (where the sound
//   effects and system textures really load) -> the big TWM logo with
//   twm_startup -> the desktop.
//
// Cross / Circle / R2 skip a section, as on PC. The FMOD logo is left out:
// this port doesn't use FMOD.
#pragma once
#include <string>

namespace boot {
// From the beginning (first start) or, after a language change, from the
// console (BootManager.RestartBootSequence).
void set_version(const char *versionString);   // top right of the BIOS console
void start();
void restart_console();
// The Customize app's Change Language: back to the language select, then on
// through the console as after a first launch's pick.
void restart_language_select();
bool active();           // drawing and updating the boot screens
bool just_finished();    // true once, the frame the sequence completes
bool show_os();          // the desktop shows under the TWM logo as it wipes away
bool take_desktop_init();   // true once, when the desktop should be created
void update();
void draw();             // whole TV (boot screen space)

// Language change from the settings menu: applied by the main loop between
// frames (the game session is dropped, then the boot console runs again).
void request_language(const std::string &code);
bool take_language_request(std::string &code);
} // namespace boot
