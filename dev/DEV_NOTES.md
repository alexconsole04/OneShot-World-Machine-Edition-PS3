# Developer notes (for resuming work)

## Workspace layout this was built in
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
- PS3:  `. /opt/ps3dk/env.sh; cmake -B build; cmake --build build`  (re-run `cmake build` after adding files)
- Host: `make -f Makefile.host -j16`  -> build-host/oneshot-host
- New source files go in BOTH CMakeLists.txt and Makefile.host.
- Keep `-fno-store-merging -fno-tree-slp-vectorize`: GCC 12 PPU miscompiles shared_ptr /
  std::function copies (64-bit ld then the low word used as a pointer). dev/scan_ld_base.py
  and dev/asmscan_ld_base.py scan the ELF/asm for that pattern.
- -fno-rtti: use Window::Kind() instead of dynamic_cast.

## Host tests
`dev/flow.sh "<input>" "f1,f2,..." ENV=...` (TIMEOUT, TAILN). Input letters:
U D L R x o s i m e l k c(R2) v z, H/J/Y/N = right stick L/R/U/D; "cH 46" = hold for 46 frames.
Envs: OSPS3_SKIP_BOOT=1, OSPS3_LAUNCH=1, OSPS3_MAP=N, OSPS3_SCREEN=1920x1080,
OSPS3_TEXTS="!script ...|...". Saves in /tmp/claude-0/saves.

## Packaging
out/mNN/README.txt + pkg + fake.self, zipped. Version string is in source/main.cpp.

## User test results (after m14f)
- Desktop puzzles, glitch effect and general performance confirmed working; game playable start to finish.
- Missing, top priority: Fast Travel, NPC followers, some graphical effects.
  - Glen fireflies: FireflyParticle.cs (30 sprites, pictures/firefly, additive, sine alpha, parallax by camDelta;
    spawned by "particles :fireflies" scripts, saved as hasFireflyParticles). Stubbed in script_parser.cpp.

- BUG (fix next build): util::random (engine/util.cpp) is never seeded, so every boot replays the
  same sequence. George's personality (var 47, rolled 1-6 on maps 51/237 when 0) barely varies.
  Seed once at startup from the clock (PC seeds System.Random from time).
- BUG: brief black screen when the boot's TWM logo wipe finishes, before the desktop shows
  (boot_sequence.cpp TWM_LOGO_FADEOUT -> COMPLETE handoff in main.cpp; desktop only drawn under the
  logo during the fadeout). Capture frames around the end of boot with flow.sh to find it.
- Before outside testing: cap oneshot_log.txt size per session (log_file.cpp only rotates at boot;
  long sessions grow it without limit). Add a build/version line + PS3 model/firmware to the log header.
- Also stubbed: mood bubbles (event_runner.cpp), footsplashes (script_parser.cpp), fast travel map (menus.cpp).

## Status (m14f) and next work (m15)
- m14f: game always drawn 1x into 640x480; Sharp = nearest 2x then linear. Awaiting PS3 test of
  maximized speed (maps 12/14/18).
- m15 plan:
  - Customize app: remove Vsync + Fullscreen (slots free); Badge Notifications; Resolution read-only;
    "Change Resolution" -> Erase All Data (two confirmations, keeps language+settings, deletes
    p-settings perma flags, desktop.dat, fs.dat, restarts as first launch, English only);
    System Scaling, View Wallpaper, Brightness, Music/Sound volume, Change Language.
  - Remove Language from the in-game Settings menu.
  - Add Quit to the game's main menu (mainmenu_quit, same as window X / CloseGame).
  - D-pad: front window owns the pad; scrolls focused non-game windows; clicking/selecting the
    game window gives it back.
  - Apps: Music Box (+ OneShot exclusion), Gallery, Friends/Contacts, Badges, Timer, Controls, Help.
- Later: wallpaper preloading (~200-270 ms hitch), custom wallpapers from /dev_hdd0/photo/wme/,
  vibration, watchdog, fast travel/followers/transitions/prefetch.
- Check SND0.AT3's origin (remove from history if it's the game's audio).
