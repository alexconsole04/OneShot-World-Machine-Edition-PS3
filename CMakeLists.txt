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
    source/game/event_data.cpp
    source/game/entity.cpp
    source/game/player.cpp
    source/game/tilemap.cpp
    source/platform/ps3/gfx_gcm.cpp
    source/platform/ps3/sys_ps3.cpp
    source/platform/ps3/input_ps3.cpp
    third_party/cJSON.c
)
target_include_directories(oneshot-ps3 PRIVATE source third_party)
target_compile_features(oneshot-ps3 PRIVATE cxx_std_17 c_std_11)
target_compile_options(oneshot-ps3 PRIVATE -O2 -Wall
    $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions -fno-rtti>)
target_link_libraries(oneshot-ps3 PRIVATE gcm_cmd gcm_sys io sysutil sysmodule rt lv2 m stdc++)

ps3_add_cg_shader_rsxcgc(oneshot-ps3 shaders/sprite_vp.vcg)
ps3_add_cg_shader_rsxcgc(oneshot-ps3 shaders/sprite_fp.fcg)
ps3_add_cg_shader_rsxcgc(oneshot-ps3 shaders/plain_fp.fcg)

ps3_add_self(oneshot-ps3)
ps3_add_pkg(oneshot-ps3
    TITLE     "OneShot PS3 (test)"
    APPID     "OSWM00001"
    CONTENTID "UP0001-OSWM00001_00-ONESHOTPS3PORT00")
