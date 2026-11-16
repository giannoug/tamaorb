// SPDX-License-Identifier: GPL-3.0-or-later
// HTTP server (page, REST API, MCP endpoint) and mDNS.
#pragma once
#include "esp_err.h"
#include "remote.h"

// mDNS <hostname>.local plus _http._tcp on port 80, then httpd on core 0. Call once, after net_start.
// No-op without Wi-Fi credentials. ops must live forever.
esp_err_t web_start(const rm_ops_t *ops, const char *hostname);
