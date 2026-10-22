# OneShot WME -> PS3 port (PPU, PS3DK toolchain).
cmake_minimum_required(VERSION 3.20)
project(oneshot-ps3 C CXX ASM)

include(ps3-self)

add_executable(oneshot-ps3
    source/main.cpp
    source/xnb.c
    source/engine/gm.cpp
    source/engine/xml.cpp
    source/engine/overlay.cpp
    source/engine/util.cpp
    source/engine/audio.cpp
    source/engine/mixer.cpp
    source/engine/ui.cpp
    source/engine/font.cpp
    source/game/event_data.cpp
    source/game/entity.cpp
    source/game/player.cpp
    source/game/tilemap.cpp
    source/game/debug_map_menu.cpp
    source/game/mouse_cursor.cpp
    source/game/event_runner.cpp
    source/game/message_box.cpp
    source/game/message_boxes.cpp
    source/game/settings.cpp
    source/game/save.cpp
    source/game/loc.cpp
    source/game/title_screen.cpp
    source/game/oneshot_window.cpp
    source/game/pictures.cpp
    source/game/script_parser.cpp
    source/game/menus.cpp
    source/game/modal.cpp
    source/game/panorama.cpp
    source/game/name_input.cpp
    source/platform/input_common.cpp
    source/platform/fs_case.cpp
    source/platform/save_io.cpp
    source/platform/log_file.cpp
    source/platform/loader.cpp
    source/platform/ps3/thread_ps3.cpp
    source/platform/ps3/gfx_gcm.cpp
    source/platform/ps3/sys_ps3.cpp
    source/platform/ps3/input_ps3.cpp
    source/platform/ps3/audio_out_ps3.cpp
    third_party/cJSON.c
)
target_include_directories(oneshot-ps3 PRIVATE source third_party)
target_compile_features(oneshot-ps3 PRIVATE cxx_std_17 c_std_11)
target_compile_options(oneshot-ps3 PRIVATE -O2 -Wall
    $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions -fno-rtti>)
target_link_libraries(oneshot-ps3 PRIVATE gcm_cmd gcm_sys io sysutil sysutil_userinfo_stub rtc_stub audio_stub fs_stub sysmodule rt lv2 m stdc++)

ps3_add_cg_shader_rsxcgc(oneshot-ps3 shaders/sprite_vp.vcg)
ps3_add_cg_shader_rsxcgc(oneshot-ps3 shaders/sprite_fp.fcg)
ps3_add_cg_shader_rsxcgc(oneshot-ps3 shaders/plain_fp.fcg)

ps3_add_self(oneshot-ps3)
# XMB extras (PIC1.PNG background, SND0.AT3 looping ambience) are copied
# from pkg_files/. SND0.AT3 is made by tests/mk_snd0.py with atracdenc.
ps3_add_pkg(oneshot-ps3
    TITLE     "OneShot: World Machine Edition (test)"
    ICON      "${CMAKE_CURRENT_SOURCE_DIR}/assets/ICON0.PNG"
    APPID     "OSWM00001"
    CONTENTID "UP0001-OSWM00001_00-ONESHOTPS3PORT00")
