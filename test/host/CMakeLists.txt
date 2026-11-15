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

enable_testing()
function(tamaorb_test name)
    add_executable(${name} ${CMAKE_CURRENT_SOURCE_DIR}/${name}.c ${CMAKE_CURRENT_SOURCE_DIR}/test_support.c ${ARGN})
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${TAMA_CORE_INC})
    target_link_libraries(${name} PRIVATE unity)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

tamaorb_test(test_rom ${TAMA_CORE_SRC})
tamaorb_test(test_core ${TAMA_CORE_SRC})
tamaorb_test(test_time ${TAMA_CORE_SRC})
