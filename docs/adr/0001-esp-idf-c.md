# ADR 0001: ESP-IDF and C

**Context.** We needed a framework for an ESP32-S3 with a GC9A01A display, an ES8311 codec, SD, Wi-Fi and fine-grained power management. The reference board code (xiaozhi-esp32) and Espressif's display and codec drivers are ESP-IDF. Arduino adds a layer without solving anything here. PlatformIO is only a build wrapper.

**Decision.** Use ESP-IDF v6.1.1 directly, in C, matching tamalib. No LVGL: the UI is a pixel grid plus a few text screens.

**Consequences.** We get first-class light sleep, power locks, NVS and the official component drivers. The UI drawing code is small and host-testable. PlatformIO support could be added later as a wrapper.
