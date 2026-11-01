# OneShot WME -> PS3 port (PPU, PS3DK toolchain).
cmake_minimum_required(VERSION 3.20)
project(oneshot-ps3 C CXX ASM)

include(ps3-self)

set(SOURCES
    source/main.cpp
    source/test_hooks.cpp
    # Engine: drawing, text, sound, file formats
    source/engine/xnb.c
    source/engine/audio.cpp
    source/engine/font.cpp
    source/engine/gm.cpp
    source/engine/mixer.cpp
    source/engine/overlay.cpp
    source/engine/ui.cpp
    source/engine/util.cpp
    source/engine/xml.cpp
    # Game: state, saves, settings, languages, scripts, the game window
    source/game/core/loc.cpp
    source/game/core/oneshot_window.cpp
    source/game/core/save.cpp
    source/game/core/script_parser.cpp
    source/game/core/settings.cpp
    # Game: maps, characters, events
    source/game/world/entity.cpp
    source/game/world/event_data.cpp
    source/game/world/event_runner.cpp
    source/game/world/follower.cpp
    source/game/world/panorama.cpp
    source/game/world/pictures.cpp
    source/game/world/player.cpp
    source/game/world/special_entities.cpp
    source/game/world/tile_layers.cpp
    source/game/world/tilemap.cpp
    source/game/world/tilemap_collision.cpp
    source/game/world/tilemap_events.cpp
    source/game/world/tilemap_load.cpp
    # Game: boot, title, menus, text boxes
    source/game/ui/boot_sequence.cpp
    source/game/ui/debug_map_menu.cpp
    source/game/ui/fast_travel_menu.cpp
    source/game/ui/menus.cpp
    source/game/ui/message_box.cpp
    source/game/ui/message_boxes.cpp
    source/game/ui/modal.cpp
    source/game/ui/mouse_cursor.cpp
    source/game/ui/name_input.cpp
    source/game/ui/title_screen.cpp
    # The World Machine desktop
    source/game/twm/fs.cpp
    source/game/twm/hintbox.cpp
    source/game/twm/icons.cpp
    source/game/twm/twm.cpp
    source/game/twm/unlocks.cpp
    source/game/twm/widgets.cpp
    source/game/twm/window.cpp
    source/game/twm/appearance.cpp
    source/game/twm/desktop_saves.cpp
    source/game/twm/desktop_test_hooks.cpp
    source/game/twm/game_window.cpp
    source/game/twm/run_file.cpp
    source/game/twm/taskbar.cpp
    source/game/twm/tutorial.cpp
    source/game/twm/wm.cpp
    # The desktop's apps
    source/game/twm/apps/collection_apps.cpp
    source/game/twm/apps/custom_wallpapers.cpp
    source/game/twm/apps/customize.cpp
    source/game/twm/apps/story_apps.cpp
    # Platform: shared
    source/platform/fs_case.cpp
    source/platform/input_common.cpp
    source/platform/loader.cpp
    source/platform/log_file.cpp
    source/platform/save_io.cpp
    source/platform/user_files.cpp
    # Platform: PS3
    source/platform/ps3/audio_out_ps3.cpp
    source/platform/ps3/gfx_gcm.cpp
    source/platform/ps3/input_ps3.cpp
    source/platform/ps3/sys_ps3.cpp
    source/platform/ps3/thread_ps3.cpp
    third_party/cJSON.c
)
add_executable(oneshot-ps3 ${SOURCES})
target_include_directories(oneshot-ps3 PRIVATE source third_party)
target_compile_features(oneshot-ps3 PRIVATE cxx_std_17 c_std_11)
# -fno-store-merging -fno-tree-slp-vectorize: GCC 12 for the PPU (32-bit pointers in
# 64-bit registers) otherwise copies pointer pairs (shared_ptr, std::function)
# with one 64-bit load and then uses half of it as an address without clearing
# the other half: an invalid address on the PS3 (the m14 startup crashes).
target_compile_options(oneshot-ps3 PRIVATE -O2 -Wall -fno-store-merging -fno-tree-slp-vectorize
    $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions -fno-rtti>)
target_link_libraries(oneshot-ps3 PRIVATE gcm_cmd gcm_sys io sysutil sysutil_userinfo_stub rtc_stub audio_stub fs_stub sysmodule rt lv2 m stdc++)

ps3_add_cg_shader_rsxcgc(oneshot-ps3 shaders/sprite_vp.vcg)
ps3_add_cg_shader_rsxcgc(oneshot-ps3 shaders/sprite_fp.fcg)
ps3_add_cg_shader_rsxcgc(oneshot-ps3 shaders/plain_fp.fcg)
ps3_add_cg_shader_rsxcgc(oneshot-ps3 shaders/dither_fp.fcg)

ps3_add_self(oneshot-ps3)
# XMB extras (PIC1.PNG background, SND0.AT3 looping ambience) are copied
# from pkg_files/ (with the port's own files under USRDIR/port). SND0.AT3 is
# made by tools/assets/mk_snd0.py with atracdenc.
ps3_add_pkg(oneshot-ps3
    TITLE     "OneShot: World Machine Edition"
    ICON      "${CMAKE_CURRENT_SOURCE_DIR}/pkg_files/ICON0.PNG"
    APPID     "OSWM00001"
    CONTENTID "UP0001-OSWM00001_00-ONESHOTPS3PORT00")
