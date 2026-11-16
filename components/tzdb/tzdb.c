// SPDX-License-Identifier: GPL-3.0-or-later
#include "tzdb.h"
#include <stddef.h>
#include <string.h>

extern const tzdb_zone_t TZDB_TABLE[];
extern const int TZDB_COUNT;

int tzdb_count(void) { return TZDB_COUNT; }
const tzdb_zone_t *tzdb_at(int i) { return (i >= 0 && i < TZDB_COUNT) ? &TZDB_TABLE[i] : NULL; }

const tzdb_zone_t *tzdb_find(const char *name)
{
    if (!name) return NULL;
    int lo = 0, hi = TZDB_COUNT - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        int c = strcmp(name, TZDB_TABLE[mid].name);
        if (c == 0) return &TZDB_TABLE[mid];
        if (c < 0) hi = mid - 1; else lo = mid + 1;
    }
    return NULL;
}

static bool is_alpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
static bool is_digit(char c) { return c >= '0' && c <= '9'; }

// 1-2 digits (or 1-3 when max > 99) within [0, max]; advances *p.
static bool num(const char **p, int max)
{
    int n = 0, d = 0;
    while (is_digit(**p) && d < 3) { n = n * 10 + (**p - '0'); (*p)++; d++; }
    return d > 0 && n <= max;
}

// [+-]hh[:mm[:ss]]
static bool hms(const char **p, int max_h, bool signed_ok)
{
    if (signed_ok && (**p == '+' || **p == '-')) (*p)++;
    if (!num(p, max_h)) return false;
    for (int i = 0; i < 2; i++) {
        if (**p != ':') break;
        (*p)++;
        if (!is_digit(**p) || !is_digit((*p)[1]) || (**p - '0') > 5) return false;
        *p += 2;
    }
    return true;
}

static bool name(const char **p)
{
    if (**p == '<') {
        const char *q = *p + 1;
        int n = 0;
        while (is_digit(*q) || is_alpha(*q) || *q == '+' || *q == '-') { q++; n++; }
        if (*q != '>' || n < 3) return false;
        *p = q + 1;
        return true;
    }
    int n = 0;
    while (is_alpha(**p)) { (*p)++; n++; }
    return n >= 3;
}

// Mm.w.d | Jn | n, optionally /[+-]hh[:mm[:ss]]
static bool rule(const char **p)
{
    if (**p == 'M') {
        (*p)++;
        const char *q = *p;
        if (!num(p, 12) || *p == q || (*p - q == 1 && *q == '0')) return false;
        if (*(*p)++ != '.') return false;
        if (!is_digit(**p) || **p < '1' || **p > '5') return false;
        (*p)++;
        if (*(*p)++ != '.') return false;
        if (!is_digit(**p) || **p > '6') return false;
        (*p)++;
    } else if (**p == 'J') {
        (*p)++;
        if (!num(p, 365)) return false;
    } else if (!num(p, 365)) {
        return false;
    }
    if (**p == '/') {
        (*p)++;
        return hms(p, 167, true);
    }
    return true;
}

bool tzdb_posix_valid(const char *s)
{
    if (!s || !name(&s) || !hms(&s, 24, true)) return false;
    if (*s == 0) return true;
    if (!name(&s)) return false;
    if (*s != ',' && *s != 0 && !hms(&s, 24, true)) return false;
    if (*s == 0) return true;
    if (*s++ != ',' || !rule(&s) || *s++ != ',' || !rule(&s)) return false;
    return *s == 0;
}

extern const char *const TZDB_REGIONS[];
extern const int TZDB_REGION_COUNT;

int tzdb_regions(const char **out, int max)
{
    int n = TZDB_REGION_COUNT < max ? TZDB_REGION_COUNT : max;
    for (int i = 0; i < n; i++) out[i] = TZDB_REGIONS[i];
    return n < 0 ? 0 : n;
}
