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
add_compile_definitions(TC_TESTING TAMAORB_ROM_DIR_DEFAULT="${CMAKE_BINARY_DIR}/roms")

# Portable sources, grouped per component. Later tasks append to these lists.
set(TAMALIB_SRC ${COMP}/tamalib/upstream/cpu.c ${COMP}/tamalib/upstream/hw.c ${COMP}/tamalib/upstream/tamalib.c)
set_source_files_properties(${TAMALIB_SRC} PROPERTIES COMPILE_OPTIONS "-Wno-maybe-uninitialized;-Wno-sign-compare;-Wno-unused-variable;-Wno-unused-but-set-variable")
set(TAMA_CORE_SRC ${COMP}/tama_core/tc_rom.c ${COMP}/tama_core/tc_sha1.c ${COMP}/tama_core/tama_core.c ${COMP}/tama_core/tc_time.c ${TAMALIB_SRC})
set(TAMA_CORE_INC ${COMP}/tama_core/include ${COMP}/tamalib ${COMP}/tamalib/upstream)

set(UI_SRC ${COMP}/ui/ui_palette.c ${COMP}/ui/ui_pet.c ${COMP}/ui/ui_icons.c ${COMP}/ui/ui_font.c ${COMP}/ui/ui_touch.c ${COMP}/ui/ui_screen.c ${COMP}/ui/ui_text.c)
set(UI_INC ${COMP}/ui/include)

set(PERSIST_SRC ${COMP}/persist/ps_record.c ${COMP}/persist/ps_policy.c)
set(PERSIST_INC ${COMP}/persist/include)

enable_testing()
function(tamaorb_test name)
    add_executable(${name} ${CMAKE_CURRENT_SOURCE_DIR}/${name}.c ${CMAKE_CURRENT_SOURCE_DIR}/test_support.c ${ARGN})
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${TAMA_CORE_INC} ${UI_INC} ${COMP}/ui ${PERSIST_INC})
    target_link_libraries(${name} PRIVATE unity)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

tamaorb_test(test_rom ${TAMA_CORE_SRC})
tamaorb_test(test_core ${TAMA_CORE_SRC})
tamaorb_test(test_time ${TAMA_CORE_SRC})
tamaorb_test(test_ui_render ${UI_SRC})
tamaorb_test(test_ui_touch ${UI_SRC})
tamaorb_test(test_ui_screen ${UI_SRC})
tamaorb_test(test_ui_text ${UI_SRC})
tamaorb_test(test_persist ${PERSIST_SRC})

# Eyeballing aid, not a test: build-host/render_preview out.ppm [held_mask]
add_executable(render_preview ${CMAKE_CURRENT_SOURCE_DIR}/render_preview.c ${UI_SRC})
target_include_directories(render_preview PRIVATE ${TAMA_CORE_INC} ${UI_INC} ${COMP}/ui)
