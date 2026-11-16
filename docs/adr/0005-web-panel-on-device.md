<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# ADR 0005: Web panel and MCP served from the ball

**Context.** The owner wanted to change settings without the USB cable, back up and restore saves, and let AI agents look at and care for the pet. The ball has 8 MB of PSRAM but only about 100 KB of free internal RAM with Wi-Fi up and the pet running, and core 1 belongs to the emulator.

**Decision.** The ball serves everything itself, open on the LAN:
- **`esp_http_server`** (part of ESP-IDF) on core 0, with 4 sockets, LRU purge and an 8 KB stack. It serves the gzipped panel page from flash, the JSON API under `/api/` and MCP (Streamable HTTP, JSON responses only) at `POST /mcp`.
- **mDNS** (`espressif/mdns`) for `<name>.local`, and **cJSON** (`espressif/cjson`). cJSON allocates from PSRAM through `cJSON_InitHooks`.
- **No login.** A request with an `Origin` header from another site gets 403, which stops web pages from driving the ball. Anything on the LAN can still use it.
- **The API, the MCP dispatcher, look and press live in `components/remote`,** which is portable and host-tested. Its large buffers come from an allocator hook (`rm_set_alloc`) that the device points at PSRAM.
- **Wi-Fi and lwIP allocate from PSRAM** (`CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP`).

**Why on the ball, and why these pieces.** A PC-side bridge would need a machine running all the time. MCP and the page need nothing beyond the LAN. All three pieces are Espressif's own and small. A spike on the ball measured:

| | Internal RAM free |
|---|---|
| Before the web layer (Wi-Fi up, pet running) | 101 KB |
| After mDNS | 94 KB |
| After httpd | 85 KB |
| Steady, idle | ≈ 90 KB |
| Lowest point during 3 min at ≈ 40 req/s | 29 KB |

Pet pace stayed within ±3 ms per minute, every request succeeded, and the firmware was 1.16 MB of the 3 MB slot.

**Load test (`tools/tamaorb-load.py`, 2026-11-16).** 4 workers for 180 s, looping over `GET /api/status`, `GET /api/look` and an MCP `tools/call look`:
- **failures = 0** (4,307 requests, 23.8 req/s);
- **every `emu: pace` line within ±0.002 s** per minute (limit ±0.010 s);
- **internal heap minimum 42.4 KB** (limit 20 KB). That is the boot minimum: the load did not lower it.

With `--large` (adding a 16,384-byte settings PATCH, which must succeed, and a 16,385-byte save PUT, which must get 413) the run had no failures either: 1,287 requests, pace within ±0.003 s, internal minimum 42.5 KB.

**Internal RAM under load.** Before Wi-Fi and lwIP moved to PSRAM, the network stack's buffers were the largest internal-RAM user under load, not our code. Four workers reading the 4.5 KB guide resource took the minimum to 19 KB, and the plain look mix to 33 KB. With the PSRAM option the same runs left the minimum where boot put it (about 42 KB). The option also changes other defaults, and they move with it if it is reverted or tuned:
- 16 static RX buffers instead of 10, in internal RAM;
- static TX buffers (16, in PSRAM) instead of 32 dynamic ones;
- an RX block-ack window of 16 instead of 6;
- `LWIP_TCP_OOSEQ_MAX_PBUFS=0`: the out-of-order TCP queue is unbounded instead of capped at 4, and backed by PSRAM.

The cost: idle free internal RAM drops from about 88 KB to 75 KB, mostly because of the extra static RX buffers. Throughput on the look mix went from 30.7 to 23.8 req/s, which a settings page and an agent don't notice.

**httpd stack: 8 KB.** On the spike's 6 KB the first load run overflowed the stack and the ball boot-looped. The MCP `look` path kept a 1 KB text buffer, a 1.6 KB PNG raster and a 1.7 KB pet list on the stack, and left 56 bytes. With 8 KB the worst high-water mark was 1,876 bytes free. Those buffers now come from the heap (the PNG raster is drawn straight into the output), so the worst case under load is about 4.4 KB free (4,420 bytes, on an MCP `list_pets`, which reads the SD card). The stack stays at 8 KB: `import_save` writes NVS and `list_pets` reads the SD card from the httpd task, so the extra margin is worth the 2 KB. A PSRAM stack is not an option for the same reason (flash writes disable the cache).

**Core 1 "busy" rises under load, and pace holds.** The emulator's busy figure went from about 9% idle to about 19% under the load test, even with lwIP, Wi-Fi, mDNS and httpd all pinned to core 0. The likely cause is shared cache and PSRAM-bus contention, which pinning can't remove. Pace is the requirement, and it held within ±0.002 s in every run. Accepted (controller ruling R14).

**Things found on the way.**
- **An unexplained reboot** during an earlier agent session could not be reproduced. About 35 minutes of bursts of look, press (A and C), MCP look, press and list_pets, guide reads and status, with a serial logger attached, gave no panic, no watchdog and no brownout. See the task-13 report.
- **`tamaorb.local` stops resolving about 2 minutes after boot on the owner's network,** with or without load; the IP keeps working. The PC is on another subnet and gets mDNS through the router. A logging build showed that after the first 120 s (the A record's TTL) the PC's queries never reach the ball, so it is the router's mDNS forwarding, not the firmware. Periodic re-announcements didn't help. On a flat network this doesn't happen; otherwise use the IP or fix the router's mDNS gateway.

**Consequences.**
- Nothing to install on a PC. Agents connect with `claude mcp add --transport http tamaorb http://tamaorb.local/mcp`.
- **Anyone on the LAN can change settings, restore saves and press buttons.** The README says so.
- Internal RAM stays above 40 KB under load, and the pet's timing is unaffected.
