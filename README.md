# OneShot-World-Machine-Edition-PS3
An experimental PlayStation 3 port of OneShot World Machine Edition, running natively on PS3 hardware. Implementing everything from the original PC release including event scripts, dialogue, world machine desktop, music, audio, etc. This port aims to research on what Claude can do with the right tools and resources to properly port a game.

This repository does not contain game files, those must be supplied from your own copy of the game. This was tested with the Steam release of the game, PS4 and Switch remain untested.

DISCLAIMER: This port was made with the assistance of Claude Opus 5.5, but all testing and debugging was done by me and tested on both RPCS3 and real PS3. If you do not want to use this port, that is perfectly fine and reasonable. The port has been verified to be beatable from start to finish on both normal and Solstice.
TLDR: this is vibecoded slop for experimental purposes.

# What isn't implemented
* Controller vibration (the setting exists; DS3 actuators via ioPadSetActDirect).
* Map transitions (SpecialTransition is a crossfade).
* Fast travel minimap solstice glitch effect. (PC uses GlitchEffect on each piece).
PS3 START/SELECT/trigger glyphs (currently still using PS4 buttons).
The game's own wallpapers load on the main thread (a hitch when switching).
Performance: dialogue hitches reported; look at the log's "slow frame" lines.

# How to Install
You need to build the PKG yourself or use the provided PKG release. Either way should be fine.
* Install the PKG onto your PS3 using FTP or USB.
* Using a file manager or FTP, place the game files inside dev_hdd0/game/OSWM00001/USRDIR (both gamedata and content)

# Known Issues
* This port does not properly render graphics on RPCS3 due to emulation bugs. Please do not create issues if playing on RPCS3, it is the limitation of the emulator, not the port.

* Some minor hitches and stutters occur during dialogue and items are obtained. This will be fixed at a later date.

# Building
See [BUILDING.md](BUILDING.md). Requires the [PS3 Custom Toolchain v0.22.0](https://github.com/FirebirdTA01/PS3DK/releases), CMake and Ninja.
How the code is organised is described in [docs/DEV_NOTES.md](docs/DEV_NOTES.md).

# License
All rights reserved; this is not an open-source project. OneShot belongs to its respective owners (Future Cat LLC, Degica and Komodo). See [LICENSE](LICENSE) for details and for the third-party licenses.
