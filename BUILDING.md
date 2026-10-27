# Building the PS3 package

## Requirements
- A Linux x86_64 machine (Windows works through WSL2, or with the toolchain's Windows build).
- CMake 3.20 or newer, and Ninja.
- PS3 Custom Toolchain v0.22.0: https://github.com/FirebirdTA01/PS3DK (releases).
  Install it to /opt/ps3dk (any folder works; adjust the paths below).

## Steps
```sh
# 1. Load the toolchain environment (every new terminal)
. /opt/ps3dk/env.sh

# 2. Configure (once)
cmake -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=$PS3DK/cmake/ps3-ppu-toolchain.cmake

# 3. Build
cmake --build build
```

The build writes, in the repository folder:
- `oneshot-ps3.pkg` - install this on the PS3 (Package Manager on CFW/HEN).
- `oneshot-ps3.fake.self` - the executable alone, for RPCS3 or manual installs.

Do not remove the `-fno-store-merging -fno-tree-slp-vectorize` flags from CMakeLists.txt:
they work around a GCC 12 code-generation bug that crashes the game.

## Game files
The package contains no game data. Copy the `content` and `gamedata` folders from your own
copy of OneShot: World Machine Edition (Steam) to:
`/dev_hdd0/game/OSWM00001/USRDIR/`
