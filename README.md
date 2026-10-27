# OneShot-World-Machine-Edition-PS3
An experimental PlayStation 3 port of OneShot World Machine Edition, running natively on PS3 hardware. Implementing everything from the original PC release including event scripts, dialogue, world machine desktop, music, audio, etc. This port aims to research on what Claude can do with the right tools and resources to properly port a game.

This repository does not contain game files, those must be supplied from your own copy of the game. This was tested with the Steam release of the game, PS4 and Switch remain untested. 
DISCLAIMER: This port was made with the assistance of Claude Opus 5.5, but all testing and debugging was done by me and tested on both RPCS3 and real PS3. If you do not want to use this port, that is perfectly fine and reasonable.

Closed source due to using original decompiled code from the Steam version's exectuable.

# How to Install
* Install the provided PKG.
* Place both "gamedata" and "content" folders inside dev_hdd0/game/OSWM00001/USRDIR

OneShot: World Machine Edition art, code, aseets belong to FutureCat.

# Building
See [BUILDING.md](BUILDING.md). Requires the [PS3 Custom Toolchain v0.22.0](https://github.com/FirebirdTA01/PS3_Custom_Toolchain/releases), CMake and Ninja.

# License
All rights reserved; this is not an open-source project. OneShot belongs to its respective owners (Future Cat LLC, Degica and Komodo). See [LICENSE](LICENSE) for details and for the third-party licenses.
