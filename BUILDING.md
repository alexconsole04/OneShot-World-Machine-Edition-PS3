# Building the PS3 package

The port builds on Windows or Linux with the PS3 Custom Toolchain v0.22.0
(https://github.com/FirebirdTA01/PS3DK, Releases page), CMake 3.20+ and Ninja.

Whichever system you use, do not remove the `-fno-store-merging -fno-tree-slp-vectorize`
flags from CMakeLists.txt: they work around a GCC 12 code-generation bug that crashes the game.

## Windows (native, no Linux needed)

1. Install CMake and Ninja, e.g. `winget install Kitware.CMake Ninja-build.Ninja`
   (or `choco install cmake ninja`, or the Visual Studio installer's CMake/Ninja components).
2. Download `ps3-sdk-v0.22.0-windows-x86_64.zip` from the toolchain's v0.22.0 release and
   extract it somewhere stable, e.g. `C:\PS3DK`.
3. In a Command Prompt, set the toolchain path once (use your folder):
   ```bat
   setx PS3DK "C:\PS3DK"
   ```
4. Open a **new** Command Prompt, then:
   ```bat
   "%PS3DK%\setup.cmd"
   cd path\to\OneShot-World-Machine-Edition-PS3
   cmake -S . -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE="%PS3DK%\cmake\ps3-ppu-toolchain.cmake"
   cmake --build build
   ```
   Run `setup.cmd` again in every new Command Prompt before building.

## Linux (x86_64, or WSL2 on Windows)

1. Install CMake and Ninja (`sudo apt install cmake ninja-build` on Debian/Ubuntu).
2. From the toolchain's v0.22.0 release, extract into one folder (here `/opt/ps3dk`):
   `ps3dev-ppu.tar.xz` and `ps3dev-spu.tar.xz` (the compilers),
   `ps3-sdk-tools-v0.22.0-linux-x86_64.tar.xz` (host tools), and the SDK's `cmake/` folder and
   headers/libraries (from `ps3-sdk-src-v0.22.0.tar.xz` or the Windows zip).
   The port's own build machine uses this combination, with an `env.sh` that sets
   `PS3DK=/opt/ps3dk` and puts `bin`, `ppu/bin` and `spu/bin` on `PATH`.
3. Build:
   ```sh
   . /opt/ps3dk/env.sh
   cmake -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=$PS3DK/cmake/ps3-ppu-toolchain.cmake
   cmake --build build
   ```

## Output

The build writes, in the repository folder:
- `oneshot-ps3.pkg` - install this on the PS3 (Package Manager on CFW/HEN).
- `oneshot-ps3.fake.self` - the executable alone, for RPCS3 or manual installs.

The Windows route has not been tried with this port yet; the toolchain documents it as
fully supported. Report problems with the full build output.

## Game files
The package contains no game data. Copy the `content` and `gamedata` folders from your own
copy of OneShot: World Machine Edition (Steam) to:
`/dev_hdd0/game/OSWM00001/USRDIR/`
