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

## Scripts in dev/
- flow.sh - run the host build with scripted input and capture screenshots (see Host tests).
- scan_ld_base.py / asmscan_ld_base.py - scan the PPU binary/asm for the GCC 12 ld-then-base miscompile.
- make_tex_xnb.py / make_font_xnb.py - make test .xnb textures/fonts. xnb_host_test.c - xnb loader test.
- clrmeta.py - minimal .NET metadata reader/IL disassembler (inspecting the PC executable).
- rrc.py / rrc_replay.py - read and replay RPCS3 RSX captures (.rrc) for GPU debugging.
- mk_snd0.py - builds pkg_files/SND0.AT3 (needs atracdenc).

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

## Status (m15)
User-confirmed on PS3/RPCS3 (m15b): followers (diagonal wobble fixed), fast travel, mood bubbles.
Firefly overlap artefact was an RPCS3 rendering bug, not the port.
Done in m15: party followers (follower.cpp), fast travel menu (fast_travel_menu.cpp), Glen fireflies
(tilemap.cpp), mood bubbles + fix_footsplashes (entity.cpp), RNG seeded from the clock
(OSPS3_SEED pins it), no black frame after the boot wipe, log rotation at 4 MB, startup traces removed.
The game's main menu already has Quit; the Language row only shows on the title screen.
Host test hooks: OSPS3_FOLLOWERS="2,3", OSPS3_TRAVEL="zone:map:x:y,...", OSPS3_FLAGS/VARS/ITEMS.

Not ported yet:
- Fast travel minimap during the solstice glitch draws plainly (PC uses GlitchEffect on each piece).
- Map transitions (SpecialTransition is a crossfade).
- (m16-m18 added Customize, the d-pad for the front window, Badges, Friends, Gallery, Music Box,
  Timer, Controls, collectibles and credits text. Test hooks: OSPS3_OPEN=customize|badges|friends|
  gallery|musicbox|timer|controls|folder:<path>|doc:<path>, OSPS3_CURSOR=x,y.)
- Gallery/Music Box solstice glitch variants are in; the gallery's LinearStretch blend for oversize
  pictures is drawn with the normal sampler.

Customize app plan: remove Vsync + Fullscreen (slots free); Badge Notifications; Resolution read-only;
"Change Resolution" -> Erase All Data (two confirmations, keeps language+settings, deletes
p-settings perma flags, desktop.dat, fs.dat, restarts as first launch, English only);
System Scaling, View Wallpaper, Brightness, Music/Sound volume, Change Language.

Later: wallpaper preloading (~200-270 ms hitch), custom wallpapers from /dev_hdd0/photo/wme/,
vibration, watchdog, prefetch. Check SND0.AT3's origin (remove from history if it's the game's audio).
