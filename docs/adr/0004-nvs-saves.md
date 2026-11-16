# ADR 0004: Saves in NVS with A/B keys

**Context.** The design asked for crash-safe, wear-levelled saves that survive the power button cutting power at any moment, and left the exact layout to the builder. It suggested a raw ring of slots.

**Decision.** Store saves in a dedicated NVS partition (`tamaorb`, 384 KB):
- **Two alternating keys per pet**, `s<10 hex of ROM SHA-1>_<pet><a|b>`.
- **Each record is CRC-protected,** with a version, a wrap-safe sequence number, a wall-clock time, and whether that time was trusted.
- **On boot the newest valid record wins.**
- **The active ROM is cached** in the same partition.

**Consequences.**
- NVS already keeps a key's previous value when power fails mid-write, and spreads wear. The A/B keys and CRC are defence in depth.
- Verified on the device with about a dozen power-button cuts, including cuts within a second of a button press.
- A pet ID is part of the key, so multiple pets per model can come later without a format change.
