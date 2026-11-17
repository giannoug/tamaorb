# SPDX-License-Identifier: GPL-3.0-or-later
idf_component_register(
    SRCS "web.c"
    INCLUDE_DIRS "include"
    REQUIRES remote
    PRIV_REQUIRES esp_http_server esp_netif esp_timer heap log espressif__cjson espressif__mdns net settings tama_core)

# The panel and the debugger are one HTML file each, gzipped and embedded; web.c serves it with Content-Encoding: gzip.
# Gzipped by CMake itself while configuring: PlatformIO's build reads this project but skips custom build
# commands, and Windows has no gzip program. Editing the page re-runs the configure step (CONFIGURE_DEPENDS).
# PlatformIO lists the output in platformio.ini (board_build.embed_files) and re-gzips it before every build
# (tools/pio_prebuild.py), because it doesn't re-run this configure step when only the page changes.
foreach(page index.html debug.html)  # the panel and the debugger
    set(page_src ${CMAKE_CURRENT_SOURCE_DIR}/page/${page})
    set(page_gz ${CMAKE_CURRENT_BINARY_DIR}/${page}.gz)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${page_src})
    file(ARCHIVE_CREATE OUTPUT ${page_gz} PATHS ${page_src} FORMAT raw COMPRESSION GZip COMPRESSION_LEVEL 9)
    target_add_binary_data(${COMPONENT_LIB} ${page_gz} BINARY)
endforeach()
