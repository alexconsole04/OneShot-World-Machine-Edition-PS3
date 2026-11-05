# Developer notes (for resuming work)

## Repository layout
```
CMakeLists.txt        PS3 build (PS3DK toolchain)
Makefile.host         Linux host build: software renderer + scripted input, for tests
BUILDING.md           how to build the pkg (Windows / Linux)
docs/                 these notes
pkg_files/            copied into the pkg: ICON0.PNG, PIC1.PNG, SND0.AT3,
                      USRDIR/port/wallpapers (wallpapers that come with the port)
shaders/              RSX Cg shaders (sprite, plain, dither)
third_party/          cJSON, stb_image, stb_image_write, stb_vorbis, stb_easy_font (+ licences)
tools/test/           flow.sh (host runs with scripted input + screenshots),
                      xnb_host_test.c, rrc.py / rrc_replay.py (RPCS3 RSX captures)
tools/assets/         make_tex_xnb.py, make_font_xnb.py (test .xnb files), mk_snd0.py (SND0.AT3)
tools/checks/         asmscan_ld_base.py / scan_ld_base.py (GCC 12 miscompile scan),
                      clrmeta.py (.NET metadata / IL reader for the PC executable)
source/
  main.cpp            start-up and the main loop
  test_hooks.cpp      the OSPS3_* switches for host tests (no-ops on the PS3)
  engine/             drawing (gm), fonts, audio + mixer, XNB/XML readers, overlay, util
  platform/           sys/gfx/input/audio/thread interfaces and the shared code;
    ps3/              the PS3 versions (RSX via gcm, pad, audio, PPU threads)
    host/             the Linux stand-ins (software renderer, scripted input)
  game/
    core/             game state, saves, settings, languages (loc), script commands,
                      the game window (oneshot_window)
    world/            maps, entities, player, followers, events, panoramas, pictures.
                      The map is TileMap, split by job: tilemap.cpp (core, update, draw),
                      tilemap_load.cpp, tilemap_collision.cpp, tilemap_events.cpp
                      (puzzles, followers, fireflies, event runners), tile_layers.cpp
    ui/               boot sequence, title screen, menus, fast travel, text boxes, dialogs,
                      name input, the in-game cursor, the debug map menu
    twm/              the World Machine desktop: windows, widgets, file system, icons,
                      unlocks, tutorial hint box; desktop.h is its interface for main.cpp.
                      The window manager is wm.cpp (windows, update, draw) with
                      appearance.cpp (themes, wallpapers, brightness/volume),
                      game_window.cpp, desktop_saves.cpp, run_file.cpp, taskbar.cpp,
                      tutorial.cpp and desktop_test_hooks.cpp; wm_state.h is their shared state
      apps/           story_apps (folders, documents, photos, journal), collection_apps
                      (Badges, Friends, Gallery, Music Box, Timer, Controls), customize,
                      custom_wallpapers
```
Build outputs (build/, build-host/, *.elf, *.self, *.pkg in the root) are ignored by git.

## Workspace this was built in
- /home/claude/osps3        - this repo
- /home/claude/src_in       - decompiled PC C# (NOT in the repo; re-supply it)
- /home/claude/gd3          - game content/ + gamedata/ used by host tests (user's own copy, NOT in the repo)
- /opt/ps3dk                - "PS3 Custom Toolchain v0.22.0" (GCC 12.4 powerpc64-ps3-elf, PSL1GHT-style).
  Source: https://github.com/FirebirdTA01/PS3DK release v0.22.0
  (ps3-sdk-src-v0.22.0.tar.xz sha256 f29a9fe0449135c970062d248d91520d76e04c4452be9e50a739890374de94c8,
   ps3-sdk-tools-v0.22.0-linux-x86_64.tar.xz sha256 8697101d31ceb684ce5897758ed0196f412d96224b71436558bc65c3b181259a)
  plus prebuilt ps3dev-ppu.tar.xz / ps3dev-spu.tar.xz compilers (ps3dev/ps3toolchain, PSL1GHT).
  Installed to /opt/ps3dk; env via /opt/ps3dk/env.sh.

## Builds
- PS3:  `. /opt/ps3dk/env.sh; cmake -B build; cmake --build build --target oneshot-ps3_pkg`
  (re-run `cmake build` after adding files)
- Host: `make -f Makefile.host -j16`  -> build-host/oneshot-host (picks up new files by folder)
- New PS3 source files go in the SOURCES list in CMakeLists.txt (grouped by folder).
- Keep `-fno-store-merging -fno-tree-slp-vectorize`: GCC 12 PPU miscompiles shared_ptr /
  std::function copies (64-bit ld then the low word used as a pointer). Check changed files:
  take the file's compile command from `ninja -C build -t commands`, swap `-c` for `-S`, drop
  `-MD -MT/-MF`, then `python3 tools/checks/asmscan_ld_base.py file.s` (0 = fine). Known harmless
  hit: gfx_gcm (1). (tilemap's TileCollision used to show 4, a register spilled with std and
  reloaded with ld; after the split into tilemap_collision.cpp it shows none.)
- -fno-rtti: use Window::Kind() / virtual queries instead of dynamic_cast.
- Background threads get 64 KB of stack unless thread_start is given more (keep big buffers off it).

## Host tests
The switches are read in source/test_hooks.cpp and source/game/twm/desktop_test_hooks.cpp
(platform ones in platform/). `tools/test/flow.sh "<input>" "f1,f2,..." ENV=...` (TIMEOUT, TAILN, WORK, GAME_DATA). Input letters:
U D L R x o s i m e l k c(R2) v z, H/J/Y/N = right stick L/R/U/D; "cH 46" = hold for 46 frames.
Envs: OSPS3_SKIP_BOOT=1, OSPS3_LAUNCH=1, OSPS3_MAP=N, OSPS3_SPAWN=x,y, OSPS3_FLAGS/VARS/ITEMS,
OSPS3_FOLLOWERS="2,3", OSPS3_TRAVEL="zone:map:x:y,...", OSPS3_SEED,
OSPS3_OPEN=customize|badges|friends|gallery|musicbox|timer|controls|folder:<path>|doc:<path>,
OSPS3_CURSOR=x,y, OSPS3_WARP="frame:x,y ..." (moves the cursor mid-run),
OSPS3_USB=<dir> (stand-in USB stick), OSPS3_WALLPAPERS=<dir>, OSPS3_PORT_DIR=<dir>.

## Versions and packaging
1.00 is the first official release; updates go 1.01, 1.02, ... The version is in
source/version.h only: the boot screen and log show it, and CMake writes it into PARAM.SFO
(cmake/sfo.xml.in, APP_VER / VERSION "01.00"). A release is out/<version>/README.txt + pkg +
fake.self, zipped.

## Port-only files (pkg_files/USRDIR/port)
- glyphs/pad_ps3_*.png: PS3 button art (START, SELECT, L1, R1, L2, R2), 32 px wide frames
  stacked vertically; drawn for the @MAINMENU/@MASHTEXT/@CLICK text tokens (font.cpp), loaded
  by gm as "port/glyphs/<name>". The other buttons use the game's PlayStation set.
- wallpapers/: wallpapers that come with the port ("We Ride at Dawn"). A renamed one goes in
  RENAMED in custom_wallpapers.cpp, so old installs and saves follow it.

## Where the player's files go (PS3)
- Saves: the system's saved data, one save OSWM00001-SAVEDATA (platform/ps3/save_data_ps3.cpp;
  file names mapped to 8.3 there). save_io.cpp keeps every save file in memory and writes
  changes on a background thread. Saves from before 1.01 (USRDIR/saves) are copied in once,
  then that folder becomes saves.old. If the save utility fails, USRDIR/saves is used as before.
- Imported wallpapers: /dev_hdd0/OneShot/wallpapers (moved from USRDIR/wallpapers once).
- Host: OSPS3_SAVEDIR (a plain folder, save_folder.cpp) and OSPS3_WALLPAPERS.

## Debug overlay (hidden)
L1 + R1 + SELECT opens it, SELECT closes it; holding SELECT for a second marks the log at any
time. While it's up: L1 = map switcher, R1 = texture viewer. Host input: "lke 5" opens it.

## Open questions / ideas
- Dragging the clover window sometimes stutters (reported once, not diagnosed).
- Performance: check the log's "slow frame" lines in players' logs.
