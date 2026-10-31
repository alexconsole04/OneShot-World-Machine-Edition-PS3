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
  main.cpp            start-up, main loop, test hooks
  engine/             drawing (gm), fonts, audio + mixer, XNB/XML readers, overlay, util
  platform/           sys/gfx/input/audio/thread interfaces and the shared code;
    ps3/              the PS3 versions (RSX via gcm, pad, audio, PPU threads)
    host/             the Linux stand-ins (software renderer, scripted input)
  game/
    core/             game state, saves, settings, languages (loc), script commands,
                      the game window (oneshot_window)
    world/            maps (tilemap), entities, player, followers, events, panoramas, pictures
    ui/               boot sequence, title screen, menus, fast travel, text boxes, dialogs,
                      name input, the in-game cursor, the debug map menu
    twm/              the World Machine desktop: window manager (wm), windows, widgets,
                      file system, icons, unlocks, tutorial hint box; desktop.h is its
                      interface for main.cpp
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
  hits: gfx_gcm (1) and tilemap's TileCollision (4: a register spilled with std and reloaded
  with ld, not a merged pointer pair).
- -fno-rtti: use Window::Kind() / virtual queries instead of dynamic_cast.
- Background threads get 64 KB of stack unless thread_start is given more (keep big buffers off it).

## Host tests
`tools/test/flow.sh "<input>" "f1,f2,..." ENV=...` (TIMEOUT, TAILN, WORK, GAME_DATA). Input letters:
U D L R x o s i m e l k c(R2) v z, H/J/Y/N = right stick L/R/U/D; "cH 46" = hold for 46 frames.
Envs: OSPS3_SKIP_BOOT=1, OSPS3_LAUNCH=1, OSPS3_MAP=N, OSPS3_SPAWN=x,y, OSPS3_FLAGS/VARS/ITEMS,
OSPS3_FOLLOWERS="2,3", OSPS3_TRAVEL="zone:map:x:y,...", OSPS3_SEED,
OSPS3_OPEN=customize|badges|friends|gallery|musicbox|timer|controls|folder:<path>|doc:<path>,
OSPS3_CURSOR=x,y, OSPS3_WARP="frame:x,y ..." (moves the cursor mid-run),
OSPS3_USB=<dir> (stand-in USB stick), OSPS3_WALLPAPERS=<dir>, OSPS3_PORT_DIR=<dir>.

## Packaging
out/mNN/README.txt + pkg + fake.self, zipped. Version string is in source/main.cpp.

## Not ported yet / ideas
- Controller vibration (the setting exists; DS3 actuators via ioPadSetActDirect).
- Map transitions (SpecialTransition is a crossfade).
- Fast travel minimap during the solstice glitch draws plainly (PC uses GlitchEffect on each piece).
- PS3 START/SELECT/trigger glyphs (the user is drawing them; PS4 OPTIONS stands in for START).
- The game's own wallpapers load on the main thread (a hitch when switching).
- Performance: dialogue hitches reported; look at the log's "slow frame" lines.
