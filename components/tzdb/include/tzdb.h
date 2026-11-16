// SPDX-License-Identifier: GPL-3.0-or-later
// Portable IANA zone name -> POSIX TZ table (generated from posix_tz_db) and POSIX TZ validation.
#pragma once
#include <stdbool.h>

typedef struct { const char *name; const char *posix; } tzdb_zone_t;   // name: IANA, e.g. "Europe/Isle_of_Man"

int tzdb_count(void);
const tzdb_zone_t *tzdb_at(int i);               // sorted by name
const tzdb_zone_t *tzdb_find(const char *name);  // exact match or NULL
// POSIX TZ grammar: std offset [dst [offset] [,rule,rule]]; names are 3+ letters or <...>.
bool tzdb_posix_valid(const char *s);
// Display helpers for the ball: region = text before the first '/', city = the rest with '_' -> ' '.
// Lists the IANA regions in order, plus "UTC" for Etc/UTC; other Etc/* zones and zones without '/' are left out.
// Returns how many were written (at most max).
int tzdb_regions(const char **out, int max);
