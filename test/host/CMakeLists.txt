# SPDX-License-Identifier: GPL-3.0-or-later
# Host (Linux) build of TamaOrb's portable components, with Unity tests.
cmake_minimum_required(VERSION 3.16)
project(tamaorb_host C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(unity
    GIT_REPOSITORY https://github.com/ThrowTheSwitch/Unity.git
    GIT_TAG v2.6.0)
FetchContent_MakeAvailable(unity)

set(ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
set(COMP ${ROOT}/components)

add_compile_options(-Wall -Wextra -Wno-unused-parameter)
add_compile_definitions(TAMAORB_ROOT_DEFAULT="${CMAKE_CURRENT_SOURCE_DIR}/../.." TC_TESTING TAMAORB_ROM_DIR_DEFAULT="${CMAKE_BINARY_DIR}/roms")

# Portable sources, grouped per component. Later tasks append to these lists.
set(TAMALIB_SRC ${COMP}/tamalib/upstream/cpu.c ${COMP}/tamalib/upstream/hw.c ${COMP}/tamalib/upstream/tamalib.c)
set_source_files_properties(${TAMALIB_SRC} PROPERTIES COMPILE_OPTIONS "-Wno-sign-compare;-Wno-unused-variable;-Wno-unused-but-set-variable"
                                                      COMPILE_DEFINITIONS OP_LOOKUP_TABLE)  # as on the device
set(TAMA_CORE_SRC ${COMP}/tama_core/tc_rom.c ${COMP}/tama_core/tc_sha1.c ${COMP}/tama_core/tama_core.c ${COMP}/tama_core/tc_time.c ${COMP}/tama_core/tc_rom_select.c ${COMP}/tama_core/tc_pet.c ${TAMALIB_SRC})
set(TAMA_CORE_INC ${COMP}/tama_core/include ${COMP}/tamalib ${COMP}/tamalib/upstream)

set(UI_SRC ${COMP}/ui/ui_palette.c ${COMP}/ui/ui_pet.c ${COMP}/ui/ui_icons.c ${COMP}/ui/ui_font.c ${COMP}/ui/ui_touch.c ${COMP}/ui/ui_screen.c ${COMP}/ui/ui_text.c ${COMP}/ui/ui_geom.c ${COMP}/ui/ui_gesture.c ${COMP}/ui/ui_qr.c ${COMP}/ui/ui_glance.c ${COMP}/ui/ui_menu.c ${COMP}/ui/ui_nav.c ${COMP}/ui/ui_send.c ${COMP}/tama_core/tc_pet.c)

set(PERSIST_SRC ${COMP}/persist/ps_record.c ${COMP}/persist/ps_policy.c)
set(PERSIST_INC ${COMP}/persist/include)

set(TZDB_SRC ${COMP}/tzdb/tzdb.c ${COMP}/tzdb/tzdb_table.c)
set(TZDB_INC ${COMP}/tzdb/include)

# settings validates tz through tzdb, so every user of settings links it too
set(SETTINGS_SRC ${COMP}/settings/st_settings.c ${TZDB_SRC})
set(SETTINGS_INC ${COMP}/settings/include ${TZDB_INC})

# ui draws the settings pages (settings, tzdb) and the glance (tc_pet); sources listed twice in a target are built once
list(APPEND UI_SRC ${SETTINGS_SRC})  # ui_nav edits through st_set
set(UI_INC ${COMP}/ui/include ${SETTINGS_INC})

set(HOSTLINK_SRC ${COMP}/hostlink/hl_proto.c ${COMP}/hostlink/hl_base64.c)
set(HOSTLINK_INC ${COMP}/hostlink/include)

FetchContent_Declare(cjson
    GIT_REPOSITORY https://github.com/DaveGamble/cJSON.git
    GIT_TAG v1.7.18
    SOURCE_SUBDIR none)  # only the sources are used, not cJSON's own CMake project
FetchContent_MakeAvailable(cjson)

set(REMOTE_SRC ${COMP}/remote/rm_look.c ${COMP}/remote/rm_png.c ${COMP}/remote/rm_press.c ${COMP}/remote/rm_save.c
    ${COMP}/remote/rm_json.c ${COMP}/remote/rm_mcp.c ${COMP}/remote/rm_http.c ${cjson_SOURCE_DIR}/cJSON.c)
set(REMOTE_INC ${COMP}/remote/include ${cjson_SOURCE_DIR})
set(REMOTE_TEST_SRC ${REMOTE_SRC} ${SETTINGS_SRC} ${CMAKE_CURRENT_SOURCE_DIR}/fake_ops.c)

enable_testing()
function(tamaorb_test name)
    add_executable(${name} ${CMAKE_CURRENT_SOURCE_DIR}/${name}.c ${CMAKE_CURRENT_SOURCE_DIR}/test_support.c ${ARGN})
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${TAMA_CORE_INC} ${UI_INC} ${COMP}/ui ${PERSIST_INC} ${HOSTLINK_INC} ${SETTINGS_INC} ${REMOTE_INC})
    target_link_libraries(${name} PRIVATE unity)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

tamaorb_test(test_rom ${TAMA_CORE_SRC})
tamaorb_test(test_core ${TAMA_CORE_SRC})
tamaorb_test(test_time ${TAMA_CORE_SRC})
tamaorb_test(test_pet ${TAMA_CORE_SRC} ${PERSIST_SRC})
tamaorb_test(test_clock ${TAMA_CORE_SRC} ${PERSIST_SRC})
# As CI runs it: no ROMs or saves, so tc_init never runs and the ROM tests skip.
add_test(NAME test_clock_no_roms COMMAND test_clock)
set_tests_properties(test_clock_no_roms PROPERTIES ENVIRONMENT "TAMAORB_ROM_DIR=/nonexistent;TAMAORB_ROOT=/nonexistent")
tamaorb_test(test_ui_render ${UI_SRC} ${SETTINGS_SRC})
tamaorb_test(test_ui_touch ${UI_SRC})
tamaorb_test(test_ui_screen ${UI_SRC})
tamaorb_test(test_ui_text ${UI_SRC})
tamaorb_test(test_ui_geom ${UI_SRC})
tamaorb_test(test_ui_gesture ${UI_SRC})
tamaorb_test(test_ui_qr ${UI_SRC})
tamaorb_test(test_ui_nav ${UI_SRC} ${SETTINGS_SRC})
tamaorb_test(test_ui_send ${UI_SRC})
tamaorb_test(test_persist ${PERSIST_SRC})
tamaorb_test(test_settings ${SETTINGS_SRC})
tamaorb_test(test_tzdb ${TZDB_SRC})
tamaorb_test(test_hostlink ${HOSTLINK_SRC} ${TAMA_CORE_SRC} ${SETTINGS_SRC})
tamaorb_test(test_remote_look ${REMOTE_TEST_SRC} ${UI_SRC} ${TAMA_CORE_SRC} ${PERSIST_SRC})
tamaorb_test(test_remote_press ${REMOTE_TEST_SRC} ${UI_SRC} ${TAMA_CORE_SRC} ${PERSIST_SRC})
tamaorb_test(test_remote_save ${REMOTE_TEST_SRC} ${UI_SRC} ${TAMA_CORE_SRC} ${PERSIST_SRC})
tamaorb_test(test_remote_mcp ${REMOTE_TEST_SRC} ${UI_SRC} ${TAMA_CORE_SRC} ${PERSIST_SRC})
tamaorb_test(test_remote_http ${REMOTE_TEST_SRC} ${UI_SRC} ${TAMA_CORE_SRC} ${PERSIST_SRC})

# Eyeballing aid, not a test: build-host/render_preview out.ppm [held_mask]
add_executable(render_preview ${CMAKE_CURRENT_SOURCE_DIR}/render_preview.c ${UI_SRC} ${SETTINGS_SRC})
target_include_directories(render_preview PRIVATE ${TAMA_CORE_INC} ${UI_INC} ${COMP}/ui)

# Dev tool, not a test: build-host/save_replay <rom.bin> <save.sav> "<script>" replays a ball save with presses
add_executable(save_replay ${CMAKE_CURRENT_SOURCE_DIR}/save_replay.c ${TAMA_CORE_SRC} ${PERSIST_SRC})
target_include_directories(save_replay PRIVATE ${TAMA_CORE_INC} ${PERSIST_INC})

# Coverage-guided parser fuzzer (not part of ctest): cmake -DTAMAORB_FUZZ=ON, then build-host/fuzz_parsers [iterations] [seed]
option(TAMAORB_FUZZ "Build the parser fuzzer with ASan/UBSan" OFF)
if(TAMAORB_FUZZ)
    set(FUZZ_SAN -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
    add_library(fuzz_targets OBJECT ${REMOTE_SRC} ${SETTINGS_SRC} ${PERSIST_SRC} ${HOSTLINK_SRC} ${CMAKE_CURRENT_SOURCE_DIR}/fake_ops.c)
    target_include_directories(fuzz_targets PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${TAMA_CORE_INC} ${UI_INC} ${COMP}/ui ${PERSIST_INC} ${HOSTLINK_INC} ${SETTINGS_INC} ${REMOTE_INC})
    target_compile_options(fuzz_targets PRIVATE ${FUZZ_SAN} -fsanitize-coverage=trace-pc -O1 -g)
    set(FUZZ_UI_SRC ${UI_SRC})
    list(REMOVE_ITEM FUZZ_UI_SRC ${SETTINGS_SRC})  # already in fuzz_targets
    add_executable(fuzz_parsers ${CMAKE_CURRENT_SOURCE_DIR}/fuzz_parsers.c $<TARGET_OBJECTS:fuzz_targets> ${FUZZ_UI_SRC} ${TAMA_CORE_SRC})
    target_include_directories(fuzz_parsers PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${TAMA_CORE_INC} ${UI_INC} ${COMP}/ui ${PERSIST_INC} ${HOSTLINK_INC} ${SETTINGS_INC} ${REMOTE_INC})
    target_compile_options(fuzz_parsers PRIVATE ${FUZZ_SAN} -O1 -g)
    target_link_options(fuzz_parsers PRIVATE ${FUZZ_SAN})
endif()
