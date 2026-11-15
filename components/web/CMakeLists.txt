# SPDX-License-Identifier: GPL-3.0-or-later
idf_component_register(
    SRCS "web.c"
    INCLUDE_DIRS "include"
    REQUIRES remote
    PRIV_REQUIRES esp_http_server esp_netif esp_timer heap log espressif__cjson espressif__mdns net settings tama_core)

# The panel is one HTML file, gzipped at build time and embedded; web.c serves it with Content-Encoding: gzip.
set(page_src ${CMAKE_CURRENT_SOURCE_DIR}/page/index.html)
set(page_gz ${CMAKE_CURRENT_BINARY_DIR}/index.html.gz)
add_custom_command(OUTPUT ${page_gz}
    COMMAND gzip -9 -n -c ${page_src} > ${page_gz}
    DEPENDS ${page_src}
    VERBATIM)
add_custom_target(web_page_gz DEPENDS ${page_gz})
add_dependencies(${COMPONENT_LIB} web_page_gz)
target_add_binary_data(${COMPONENT_LIB} ${page_gz} BINARY)
