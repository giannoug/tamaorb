# TamaOrb: Web panel, HTTP API and MCP

Date: 2026-11-15
Status: approved by the owner, 2026-11-15
Builds on: `2026-11-15-tamaorb-design.md` (the v1 spec, "the v1 spec" below), §8 and §12.

As in the v1 spec, the markers are:
- **(decided)**: the owner chose it;
- **(measured)**: verified on the ball;
- **(builder's call)**: chosen during design and not explicitly confirmed by the owner. It may change if implementation shows something better.

---

## 1. Intent

The ball gets a network face, so the owner doesn't need the USB cable or a reflash for everyday changes. It serves three needs **(decided)**:

1. **Change settings:** brightness, dim level, dim and off delays, volume, LCD palette, device name, NTP server, Wi-Fi.
2. **Manage the ball:**
   - see status;
   - list the pets (ROMs) and switch between them;
   - back up and restore a pet's save.
3. **Let an agent look after the pet.** An agent on a PC on the LAN (e.g. Claude Code) connects to the ball's own MCP server. It can:
   - report how the pet is doing;
   - do the care (feed, clean, medicine, games) by pressing A/B/C;
   - manage the ball when asked.

**Remote play is for agents only (decided).** Agents can read the screen and press buttons. The web page shows settings, status and management, not the game screen. People still play on the ball.

## 2. Goals, non-goals, anti-goals

### Goals

1. A web page served by the ball at `http://<name>.local/`. It's one scrolling page, and settings apply live (§6). **(decided: layout A)**
2. A JSON HTTP API that the page uses and scripts can use (§7).
3. An MCP server on the ball at `/mcp`, with raw controls plus a how-to-play guide (§8). **(decided)**
4. Settings stored on the device, replacing today's compile-time constants (§5). **(decided)**
5. The same settings also readable and writable over USB with `tools/tamaorb-rom.py`, for when there's no Wi-Fi (§9). **(decided)**

### Non-goals (this round)

All of these were left out by the owner unless marked otherwise:
- **Alerts or push notifications** (phone push, waking an agent). The owner said "make the MCP, we'll figure it out later". **(decided)**
- **Care macros** (`feed`, `clean`, …) on the ball. Agents use raw button presses. **(decided)**
- **Decoded pet stats** read from ROM RAM. Agents read the screen only. **(decided)**
- **Uploading or removing ROMs over the network.** That stays on USB. **(decided)**
- **OTA firmware updates.**
- **Several pets per model.**
- **Authentication or HTTPS.** The LAN is trusted. **(decided)**
- **A live game screen or buttons in the browser.** **(decided)**
- **A Wi-Fi access-point or captive-portal fallback.** USB covers the no-Wi-Fi case. **(decided)**

### Anti-goals (a failure even if it "works")

- **Disturbing the emulator in any way.** That means stutter, pace drift, or work on core 1.
- **Losing or silently replacing a pet** through a switch, a save restore, or a failed request.
- **A page that needs the internet,** such as CDN scripts or web fonts.
- **Wearing out flash** because a slider writes NVS on every movement.
- **Making the Wi-Fi password readable** through any API.
- **Shipping Bandai material.** That includes ROMs, artwork, and text copied from manuals in the guide.
- **Agent activity that changes what's on the ball.** Agent presses don't wake the screen, show a marker, or count as activity for dimming. **(decided: "nothing visible")**

## 3. Constraints

- All the v1 spec's constraints apply: ESP-IDF 6.1.1, C, no LVGL, GPL-3.0-or-later, no ROMs, offline-tolerant.
- **Works without the internet.** The panel and the MCP server need only the LAN. Without Wi-Fi at all, the USB tool covers settings (§9).
- **New dependencies,** both Espressif's own managed components, and nothing else:
  - `espressif/mdns`, for `<name>.local`;
  - `espressif/cjson`, for JSON. cJSON left core ESP-IDF in v6.
  - `esp_http_server` is part of ESP-IDF.
- **Core 0 only** for all web, MCP and settings work. The emulator stays alone on core 1.

### 3.1 RAM spike (measured, 2026-11-15)

A throwaway build on the ball ran `esp_http_server` (4 sockets, 6 KB stack, core 0), mDNS and cJSON. cJSON's allocations were redirected to PSRAM with `cJSON_InitHooks`. Results:

| | Internal RAM free |
|---|---|
| Before the web layer (Wi-Fi up, pet running) | 101 KB |
| After mDNS | 94 KB |
| After httpd | 85 KB |
| Steady, idle | ≈ 90 KB |
| Lowest point during 3 min at ≈ 40 req/s (GETs plus 3 KB JSON-RPC POSTs) | 29 KB. The largest free block stayed at 31 KB. |

- **Pet pace was unaffected,** within ±3 ms per minute, idle and under load.
- **Every request succeeded:** 120 parallel requests all returned 200.
- **Core 1 got busier,** "busy" rising from ≈ 12% to ≈ 21% under load. The likely cause is the lwIP/Wi-Fi tasks being scheduled on core 1 (see §10.3).
- **Firmware size:** 1.16 MB of the 3 MB app slot.
- `tamaorb.local` resolved from the owner's Windows PC.

## 4. What the owner will see

1. Open `http://tamaorb.local/` in a browser on the LAN. The status, pets and settings cards appear.
2. Drag the brightness slider. The ball's backlight follows, and the value is still there after a reboot.
3. Run `claude mcp add --transport http tamaorb http://tamaorb.local/mcp` once. In Claude Code, ask "how is my pet?" or "feed it a meal". The agent reads the screen and presses buttons; the ball itself shows nothing special.
4. With Wi-Fi down, run `tools/tamaorb-rom.py set brightness=40` over the cable.

## 5. Settings

### 5.1 The list

| Key | Meaning | Range | Default | Applies |
|---|---|---|---|---|
| `brightness` | Backlight when on, % | 1–100 | 80 | live |
| `dim_level` | Backlight when idle-dimmed, % | 0–100, clamped to ≤ `brightness` | 5 | live |
| `dim_usb_s` | Idle time before dimming on USB, s | 0 (never) or 5–3600 | 30 | live |
| `dim_battery_s` | Idle time before dimming on battery, s | 0 (never) or 5–3600 | 15 | live |
| `off_battery_s` | Idle time before the screen turns off on battery, s | 10–3600 | 30 | live |
| `volume` | Speaker volume, %; 0 mutes and turns the amp off | 0–100 | `CONFIG_TAMAORB_VOLUME` (1) | live |
| `palette` | LCD colours | `classic`, `neutral`, `night` | `classic` | live |
| `name` | Device name: DHCP hostname and mDNS `<name>.local` | 1–32 chars, `a-z 0-9 -`, not starting or ending with `-` | `tamaorb` | after restart |
| `ntp` | NTP server | hostname or IPv4, ≤ 63 chars | `pool.ntp.org` | after restart |
| `agent_feedback` | What the ball does with beeps caused by agent presses (§8.2) | `quiet`, `silent`, `visible` | `quiet` | live |

Wi-Fi credentials are not in this table. They are written through their own operation (§7), as today, and **the password is write-only**: no API or USB command ever returns it.

- **On USB the screen never turns off by itself.** That's v1 behaviour, kept. **(decided in v1)**
- **Off-delay range:** `off_battery_s` has no "never" option, to protect the battery. **(builder's call)**
- **Dim delay vs off delay:** if `dim_battery_s` ≥ `off_battery_s`, the screen simply turns off without dimming first. **(builder's call)**

**Palettes.** Only Classic exists in the firmware today. Neutral and Night are defined in the owner's design canvas, "TamaOrb Display" (the link is in the v1 spec §5.1, file `project/Palettes.dc.html`). Port their background, ink and ghost values exactly from there. **(decided: palette is a setting)**

**Agent feedback** is set from the Agents card. **(decided: "make it a configuration in the agents pane")** The default `quiet` matches the owner's earlier "nothing visible". **(builder's call on the default)** The three values are defined in §8.2.

### 5.2 Behaviour

- **Live apply.** A change takes effect within one UI loop (≈ 30 ms).
  - The UI task reads brightness, dim, delays and palette from the settings module on each loop, instead of the `#define`s in `main/ui_task.c`.
  - A palette change forces a full redraw.
  - Volume calls `board_volume_set`, which already turns the amp off at 0.
- **A display change made from the web or USB wakes the screen.** That covers brightness, dim level and palette, so the owner can see the effect. This is a deliberate exception to "agent activity is invisible". It applies only to setting changes, never to button presses. **(builder's call)**
- **Persistence.**
  - **Where:** the default `nvs` partition, namespace `settings`, one key per setting. That's the same partition as the Wi-Fi credentials, not the `tamaorb` save partition. **(builder's call)**
  - **When:** changes are written **2 s after the last change** (debounced), so dragging a slider causes one write.
  - **Power cut:** a cut inside that window loses only the unsaved change.
- **Defaults and bad values.** A missing or invalid stored value falls back to its default. Settings survive reflashing.
- **Validation lives in one place**, a portable settings module, so the web, MCP and USB faces accept and reject exactly the same things.
- **"After restart" settings** (`name`, `ntp`) are stored immediately. The page and the API say a restart is needed and offer a restart action.

## 6. The web page

Layout A from the mockups: one scrolling page of cards. **(decided)**

**Visual reference: the design canvas "TamaOrb Web Panel"** (https://claude.ai/artifact/2rqMHy3QAGosXXzhTjnWaW, private to the owner). **(decided: the page is designed with the design tool)** It holds:
- the panel at desktop width (interactive; tweaks show battery power and catching up);
- the panel at phone width;
- the save-restore dialog;
- the restarting overlay;
- a **Components** page:
  - **Tokens:** colours named as CSS custom properties (`--ink`, `--accent` …), the three LCD palettes, the type scale, radii and spacing;
  - **Components:** each part with its states and when to use it.

The embedded page defines the tokens as CSS custom properties with those names.

Copy layout, colours and wording from the canvas. Changes happen there first, and the owner reviews them, as for the device screens.

- **Status:** device name, firmware version, uptime, and a phase banner (catching up with a progress %, or running).
  - Active pet: model and ROM file.
  - Power: USB or battery, battery mV and %.
  - Wi-Fi: SSID, RSSI, IP.
  - Time: NTP trusted or not, plus the current time.
  - Last save time; SD card present.
  - It refreshes every 5 s **(builder's call)**.
- **Pets:**
  - Each ROM on the SD card is shown with its model. The active one is marked. If there's no card, the cached active ROM is listed alone.
  - **Switch** asks for confirmation, then saves, reboots, waits and reconnects.
  - **Save ↓** downloads the pet's newest save.
  - **Restore ↑** uploads a save (§7.3).
- **Display:** brightness and dim-level sliders, the three delays, and the palette choice.
- **Sound:** a volume slider.
- **Network:** device name, NTP server, a "Change Wi-Fi…" form (SSID and password; reboots), and "Restart".
- **Agents:** the `claude mcp add …` line ready to copy, with the ball's actual name filled in, and the **agent feedback** choice (`quiet` / `silent` / `visible`, §8.2), each with a one-line explanation.

**Rules:**
- **One self-contained file.** HTML, CSS and JS in a single file, embedded in the firmware (gzipped, served with `Content-Encoding: gzip`). No external requests at all. Plain JS, no framework.
- **Live sliders.** Sliders send changes while dragging, throttled to about 10 per second. The device debounces the NVS writes.
- **Reboots.** Any action that reboots the ball shows a "restarting…" overlay, then polls `/api/status` until the ball answers again.
- **Phone width.** It must work at phone width.
- **Look.** As in the design canvas: system fonts only (no web fonts, because the page is offline), the LCD greens, and the header orb drawn in the active palette.

## 7. HTTP API

JSON over plain HTTP, no auth. **(decided: open on the LAN)** The exact paths and field names are the builder's call; the operations are not.

| Operation | Suggested route | Notes |
|---|---|---|
| Page | `GET /` | the embedded page |
| Status | `GET /api/status` | the fields in §6 Status, plus `internal_heap_free` / `_min` for diagnostics, and the last measured catch-up speed (× real time; the v1 figure until a catch-up has run this boot) |
| Read settings | `GET /api/settings` | all keys in §5.1, plus which ones are pending a restart |
| Change settings | `PATCH /api/settings` | partial object; all-or-nothing validation; returns the new settings or a 400 naming the bad key |
| Set Wi-Fi | `POST /api/wifi` | `{ssid, password}`; replies, then reboots ≈ 1 s later |
| Restart | `POST /api/restart` | replies, then reboots |
| List pets | `GET /api/pets` | per ROM: file name, model, SHA-1, active, whether a save exists and its time |
| Switch pet | `POST /api/pets/switch` | `{name}`; same path as the on-device picker: save current pet, set active ROM, reboot |
| Download save | `GET /api/pets/<sha1>/save` | the newest valid record, as is (§7.3) |
| Restore save | `PUT /api/pets/<sha1>/save` | upload a record (§7.3) |
| Look | `GET /api/look` | same content as the MCP `look` tool (§8.2) |
| Press | `POST /api/press` | same as the MCP `press` tool |

**Rules:**
- **Replies before reboots.** Every reboot is scheduled about 1 s after the response has been sent.
- **Request size.** Bodies are capped at 16 KB, and anything bigger is rejected (413).
- **Rejected while catching up:** switch and restore are rejected (409) while the emulator is catching up, matching the picker. Settings, status and look still work. Press is ignored during catch-up, as touches are.
- **Cross-site protection.** Mutating requests and `/mcp` that carry an `Origin` header must match the ball's own host (name, `name.local` or IP); otherwise they get 403. This stops a web page in the owner's browser from driving the ball through DNS rebinding or cross-site POSTs, and it costs nothing in a no-auth design. Requests without `Origin` (curl, agents) are allowed. **(builder's call; the MCP spec requires Origin validation)**

### 7.1 Switching pets

This reuses the existing switch logic: save, `rom_store_set_active`, reboot. Catch-up on the next boot works as in v1: the newly active pet catches up from its own last save.

### 7.2 Status during boot

The HTTP server starts as soon as the network is up, which can be before or during catch-up. Status reports the phase and the catch-up progress, so an agent or the page can tell the ball is busy.

### 7.3 Save backup and restore

- **The download** is the raw v1 save record: header, payload and CRC, about 1 KB. The file name is `tamaorb-<model>-<sha1 first 8>-<UTC date>.sav`. **(builder's call)**
- **An upload is accepted only if all of these hold:**
  - the CRC is valid;
  - the format version is one this build can load;
  - the SHA-1 in the record matches the URL's SHA-1;
  - that SHA-1 is a supported model (P1/P2).

  The ROM doesn't have to be on the card; the save waits for it.
- **Storing it.** The restored record is written as that pet's **newest** record, with sequence = current newest + 1. The previous newest stays in the other A/B slot.
- **If it's the active pet,** the ball must reboot straight into the restored save. The running emulator **must not** save over it before the reboot. The usual "save before reboot" is skipped for this path, and the saver is stopped first.
- **The restore's time is chosen on every restore. (decided: "ask each time")** The upload carries a required choice. The API takes it as a query parameter or a header; that's the builder's call.
  - **`frozen`:** the record is re-stamped with the ball's current time and trust flag, so the pet resumes exactly as in the backup, with no catch-up. If the ball has no trusted time yet, the record is stored with time "untrusted", which means no catch-up later either (v1 spec §6.4).
  - **`replay`:** the record keeps its original time and trust flag, so the next boot replays everything since the backup.
- **Before uploading, the page reads the backup's age** from the file header, in the browser, and asks. It shows the age and an estimated catch-up time (age ÷ the measured catch-up speed, ≈ 24×; reported by `/api/status` so it stays right after tamalib speeds up). It also warns that the pet may not survive a long replay. A request without the choice is rejected (400).

## 8. MCP server

### 8.1 Transport

- **Endpoint:** MCP Streamable HTTP at `POST /mcp`. Every request gets a single `application/json` response; there is no SSE stream. `GET /mcp` returns 405. **(builder's call; allowed by the spec)**
- **Stateless:** no `Mcp-Session-Id`.
- **Methods:**
  - `initialize`: negotiate the protocol version (echo the client's version if supported, otherwise our latest), plus a short `instructions` string: "Read the guide resource for the active model before acting."
  - `notifications/initialized`: reply 202 with no body.
  - `ping`, `tools/list`, `tools/call`, `resources/list`, `resources/read`.
  - Anything else gets a JSON-RPC "method not found".
- **Errors:** batches are rejected. Tool errors come back as `isError: true` results with a readable message, not as transport errors.
- **The MCP dispatcher** (JSON-RPC in, JSON-RPC out, against an operations table) **is portable** and host-tested. Only the HTTP glue is ESP-IDF code.

### 8.2 Tools (raw controls, decided)

| Tool | Input | Output |
|---|---|---|
| `look` | — | the screen (see below) |
| `press` | `buttons`: a sequence such as `"A A B"` or `["A","AC","B"]`; optional `hold_ms` (default 150), `gap_ms` (default 200), `settle_ms` (default 500) | runs the sequence, waits `settle_ms`, then returns `look` |
| `status` | — | the status object (§6) |
| `get_settings` | — | settings (§5) |
| `set_settings` | partial settings | new settings, or an error naming the bad key |
| `list_pets` | — | as `GET /api/pets` |
| `switch_pet` | `name` | confirmation; the ball reboots. The description warns that this triggers a catch-up |

**`look` returns** (exact layout is the builder's call):
- a text block with:
  - the 32×16 LCD as 16 lines of 32 chars, `#` for lit and `.` for unlit;
  - the lit icons **by name**, in ROM order: `food`, `light`, `game`, `medicine`, `bathroom`, `meter`, `discipline`, `attention`;
  - buzzer on/off;
  - the emulator phase, and the catch-up % when catching up;
  - the frame sequence number, so a polling agent can tell whether anything changed;
- an `image/png` of the LCD, upscaled with hard edges and the icon row included, for multimodal agents. A tiny stored-deflate PNG encoder is enough. **(builder's call)**

**`press` rules:**
- **Steps.** Each step is `A`, `B`, `C` or `AC` (A and C together). The sequence is at most 32 steps, `hold_ms` is 50–2000, and the whole call is capped at about 10 s.
- **Timing runs on the ball.** HTTP round trips are too jittery for holds.
- **Agent buttons are ORed with touch.** Today the UI task calls `tc_post_buttons(out.buttons)` every loop, which would overwrite an agent's press. The UI task must post `touch | agent` instead, with the agent mask set by the press runner. If the owner touches during an agent press, both apply. **(builder's call)**
- **Invisible on the ball.** Presses don't wake the screen, don't reset the idle timers and don't show anything. **(decided)**
- **The beeps they cause follow the `agent_feedback` setting.** Most presses make the ROM click (≈ 54 ms), and in v1 any beep that starts wakes and undims the screen (`ui_screen_update`). A beep counts as "agent-caused" if it starts during an agent sequence or within `settle_ms` after it. Exact window: builder's call.
  - **`quiet`** (default): agent-caused beeps play but don't wake or undim the screen. Other beeps, like the pet calling, wake it as before. This needs an "agent-caused" input to the portable `ui_screen` state machine, with tests.
  - **`silent`:** as `quiet`, and the speaker is also muted during agent sequences. The mute is one atomic flag checked in the tone callback on the emulator task, nothing more, so core 1 does no extra work. Known trade-off: a real call that starts during an agent sequence is silent too. It still shows the attention icon, which `look` reports.
  - **`visible`:** agent-caused beeps behave like any beep: they play, wake and undim.
- **No dark-screen rule for agents.** The v1 rule "a touch on a dark screen only wakes it" protects humans from pressing blindly. It does not apply to agents, which can see the LCD through `look` whatever the backlight does.
- **Saving.** Agent presses do trigger the normal "save 3 s after the last button release" (v1 spec §6.2), so care done by an agent is saved like a human's.
- **One at a time.** Only one press sequence runs at a time; a second one gets an error ("busy").

### 8.3 Resources

- `tamaorb://guide/p1` and `tamaorb://guide/p2`, Markdown, each a few KB, written by us and reviewed by the owner. Each covers:
  - what each icon means and how to open it (A cycles, B selects, C cancels);
  - the meal and snack, light, game, medicine, toilet, meter and discipline flows;
  - what the attention icon means and the usual reasons for it;
  - the hunger and happiness hearts in the meter screens;
  - the P1/P2 differences.

  No text copied from Bandai manuals. **(decided: raw controls plus a guide)**
- `tamaorb://status`, the same object as the `status` tool. Optional.

## 9. USB: settings over the cable

**(decided)** The existing hostlink protocol gains two commands. The framing details are the builder's call, in the style of `hostlink.h`:

```
@TO GET                  -> @TO SET <key>=<value> … then @TO OK
@TO SET <key>=<value> …  -> @TO OK | @TO ERR <key>
```

The tool gains:
- `tamaorb-rom.py get`, which prints all settings;
- `tamaorb-rom.py set key=value …`.

Values are parsed and validated by the same settings module as §5.2. The tool keeps its name. **(builder's call)**

## 10. Architecture

### 10.1 New pieces (names are the builder's call)

- **`components/settings`** (portable, host-tested): the table in §5.1 (defaults, ranges, validation), key=value and JSON-object parse and format, change notification, and the debounced-write policy. It reaches NVS through a thin storage interface, as `persist` does.
- **`components/remote`** (portable, host-tested):
  - the operations table that the web, MCP and USB faces call, like `hl_ops_t` today;
  - the MCP JSON-RPC dispatcher;
  - the press-sequence parser and runner logic;
  - the `look` text formatter and PNG encoder;
  - save-upload validation.

  It uses cJSON. The host build needs cJSON too, as a test-only copy or via the component.
- **`components/web`** (ESP-IDF): `esp_http_server` routes, the embedded page, mDNS start and hostname, and calls into `remote`.
- **`main/`**: wires the operations to `rom_store`, `saver`, `net`, `power`, `tama_core` and the UI, as `hostlink_task.c` does now. The existing switch, save and Wi-Fi code is reused, not copied.
- **`net`**: takes the hostname and NTP server from settings. It starts mDNS once it has an IP, and the HTTP server after `net_start`.

### 10.2 Threads and cores

- **httpd on core 0,** with `max_open_sockets = 4`, LRU purge on, and a stack of about 6–8 KB, as measured.
- **The press runner** is a small core-0 task, or esp_timer callbacks. The HTTP handler waits for it to finish, so a long `press` blocks other HTTP requests for its duration (≤ 10 s). That's acceptable; see §11.
- **Settings reads** are lock-free snapshots or a short critical section. The UI task reads them every loop.

### 10.3 RAM and CPU rules (from §3.1)

- **JSON in PSRAM.** cJSON uses PSRAM through `cJSON_InitHooks`. Request bodies over a few hundred bytes are read into PSRAM buffers.
- **No large internal-RAM buffers** in the web layer. The page is served straight from flash.
- **Network stack on core 0.** Pin the lwIP TCP/IP task and the Wi-Fi task to core 0 (`CONFIG_LWIP_TCPIP_TASK_AFFINITY_CPU0`, plus the Wi-Fi task core setting). Then re-measure: core-1 "busy" under load should go back to the idle figure.
- **Acceptance under the load test (§11):**
  - internal-RAM minimum free stays **≥ 20 KB**;
  - emulator pace stays within **±0.01 s per minute**;
  - no failed requests at 4 parallel connections.

### 10.4 Power

- **Power management.** On battery, automatic light sleep and Wi-Fi modem sleep stay as in v1. The server must keep working, and slower replies are acceptable.
- **Screen.** The web layer never holds the screen-on lock, except for the display-setting wake in §5.2.
- **Low battery.** Deep sleep stops everything as before.

## 11. Testing

**Host tests (Unity, CI):**
- settings: defaults, every range edge, clamping `dim_level` to brightness, partial updates being all-or-nothing, the debounce policy, and stored garbage falling back to defaults;
- MCP dispatcher: golden JSON-RPC exchanges for `initialize`, `tools/list`, each tool (against a fake operations table), unknown method, malformed JSON, batch rejected, and the notification returning nothing;
- the press-sequence parser and limits; the agent mask ORed with touch;
- `look` text art and a PNG decode round trip;
- save upload: valid, bad CRC, wrong SHA-1, unknown model, future version; `frozen` re-stamps the time (trusted and untrusted "now"), `replay` keeps it, and a missing choice is rejected;
- `ui_screen`: agent-caused beeps under `quiet` and `silent` neither wake nor undim the screen, while other beeps still do; under `visible` they wake as in v1.

**Tool tests (pytest):** `get` and `set` against a fake serial device, as the existing tests do.

**On the ball (manual checklist):**
- the page at `tamaorb.local` from a PC and a phone;
- every setting applies live and survives a reboot;
- `name` and `ntp` apply after a restart;
- Wi-Fi change, including a wrong password and recovery over USB;
- switching pets from the page and from MCP;
- save download, then restore to the active pet (the pet really reverts) and to an inactive one;
- `claude mcp add`, then a real care session: check the hunger meter, feed a meal, clean up;
- the load test: a script (kept in `tools/`) hammers status, look and MCP for 3 min while the serial log shows heap and pace; the acceptance numbers are in §10.3;
- a power-button cut during a settings drag, and during a restore.

## 12. Documentation

- **README:** a "Web panel and agents" section covering the URL, the `claude mcp add` line, `tamaorb-rom.py get/set`, and that it's open on the LAN.
- **ADR 0005:** web layer and MCP served from the ball, open on the LAN, `esp_http_server` + mDNS + cJSON, and why (§3.1).
- **v1 spec:** in §12 and §5.6, point "owner-requested settings" and "volume via the web UI" here, and remove `TAMAORB_VOLUME` as the only volume control. It stays as the default.

## 13. Relation to the pending tamalib branch

This work doesn't depend on the fixed tamalib branch the owner is preparing (project memory: "Pending tamalib integration"), and either can land first. **(builder's call)**

- **If tamalib lands first** and bumps `STATE_VERSION`, restore must accept every version the build can load. Host tests cover a v1 record and a current one.
- **If this lands first,** the tamalib integration only has to keep v1 saves loadable, as its own steps already require.
- **The catch-up speed** shown in the restore warning comes from `/api/status`, so it updates by itself.

## 14. Left to the builder

- Exact routes, JSON field names, MCP tool and parameter names, and the press defaults.
- Page styling within §6, and the status refresh interval.
- NVS key names, how the settings snapshot is shared, and debounce details.
- How cJSON reaches the host build.
- PNG scale and layout.
- Guide wording (the owner reviews it).
- Whether `tamaorb://status` exists as a resource.
