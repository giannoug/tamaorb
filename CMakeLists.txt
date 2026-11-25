# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.16)

# The firmware version is the commit it is built from, plus "-dirty" (tools/version.py). Set before project() so
# esp_app_get_description()->version carries it; tools/pio_prebuild.py reconfigures when it changes.
find_program(TAMAORB_PYTHON NAMES python3 python)
if(DEFINED ENV{PYTHON})
    set(TAMAORB_PYTHON $ENV{PYTHON})
endif()
execute_process(COMMAND ${TAMAORB_PYTHON} ${CMAKE_CURRENT_LIST_DIR}/tools/version.py ${CMAKE_CURRENT_LIST_DIR}
                OUTPUT_VARIABLE PROJECT_VER RESULT_VARIABLE tamaorb_version_rc)
if(NOT tamaorb_version_rc EQUAL 0 OR PROJECT_VER STREQUAL "")
    set(PROJECT_VER "unknown")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(tamaorb)
file(WRITE ${CMAKE_BINARY_DIR}/tamaorb_version.txt "${PROJECT_VER}")  # what this configure saw
