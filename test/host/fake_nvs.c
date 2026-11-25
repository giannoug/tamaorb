// SPDX-License-Identifier: GPL-3.0-or-later
// An in-memory NVS for host tests of main/ (see fakes/nvs.h for the edges it copies from ESP-IDF).
#include "fake_nvs.h"
#include <stdlib.h>
#include <string.h>
#include "nvs.h"

#define MAX_ENTRIES 128
#define MAX_NS      16
#define MAX_HANDLES 16

typedef struct { bool used; char part[16], ns[16], key[16]; uint8_t *data; size_t len; } entry_t;
typedef struct { bool used; char part[16], ns[16]; } ns_t;
typedef struct { bool used; int ns; nvs_open_mode_t mode; } handle_t;
struct fake_nvs_it { char part[16], ns[16]; bool any_ns; int idx; };

static entry_t s_e[MAX_ENTRIES];
static ns_t s_ns[MAX_NS];
static handle_t s_h[MAX_HANDLES];
static bool s_fail_writes;
static int s_writes, s_reads;

void fake_nvs_reset(void)
{
    for (int i = 0; i < MAX_ENTRIES; i++) free(s_e[i].data);
    memset(s_e, 0, sizeof s_e);
    memset(s_ns, 0, sizeof s_ns);
    memset(s_h, 0, sizeof s_h);
    s_fail_writes = false;
    s_writes = s_reads = 0;
}

void fake_nvs_fail_writes(bool fail) { s_fail_writes = fail; }
int fake_nvs_writes(void) { return s_writes; }
int fake_nvs_reads(void) { return s_reads; }

bool fake_nvs_has(const char *ns, const char *key)
{
    for (int i = 0; i < MAX_ENTRIES; i++)
        if (s_e[i].used && !strcmp(s_e[i].ns, ns) && !strcmp(s_e[i].key, key)) return true;
    return false;
}

static int find_ns(const char *part, const char *ns)
{
    for (int i = 0; i < MAX_NS; i++)
        if (s_ns[i].used && !strcmp(s_ns[i].part, part) && !strcmp(s_ns[i].ns, ns)) return i;
    return -1;
}

static entry_t *find_entry(int ns, const char *key)
{
    for (int i = 0; i < MAX_ENTRIES; i++)
        if (s_e[i].used && !strcmp(s_e[i].part, s_ns[ns].part) && !strcmp(s_e[i].ns, s_ns[ns].ns) &&
            !strcmp(s_e[i].key, key))
            return &s_e[i];
    return NULL;
}

static handle_t *handle(nvs_handle_t h) { return h >= 1 && h <= MAX_HANDLES && s_h[h - 1].used ? &s_h[h - 1] : NULL; }

esp_err_t nvs_open_from_partition(const char *part, const char *ns, nvs_open_mode_t mode, nvs_handle_t *out)
{
    int n = find_ns(part, ns);
    if (n < 0) {
        if (mode == NVS_READONLY) return ESP_ERR_NVS_NOT_FOUND;  // as ESP-IDF: a read-only open doesn't create
        for (n = 0; n < MAX_NS && s_ns[n].used; n++) {}
        if (n == MAX_NS) return ESP_ERR_NVS_NOT_ENOUGH_SPACE;
        s_ns[n].used = true;
        strncpy(s_ns[n].part, part, sizeof s_ns[n].part - 1);
        strncpy(s_ns[n].ns, ns, sizeof s_ns[n].ns - 1);
    }
    for (int i = 0; i < MAX_HANDLES; i++)
        if (!s_h[i].used) {
            s_h[i] = (handle_t){ true, n, mode };
            *out = (nvs_handle_t)(i + 1);
            return ESP_OK;
        }
    return ESP_ERR_NO_MEM;
}

void nvs_close(nvs_handle_t h)
{
    handle_t *p = handle(h);
    if (p) p->used = false;
}

esp_err_t nvs_get_blob(nvs_handle_t h, const char *key, void *out, size_t *len)
{
    s_reads++;
    handle_t *p = handle(h);
    if (!p) return ESP_ERR_INVALID_ARG;
    entry_t *e = find_entry(p->ns, key);
    if (!e) return ESP_ERR_NVS_NOT_FOUND;
    if (!out) {
        *len = e->len;
        return ESP_OK;
    }
    if (*len < e->len) return ESP_ERR_INVALID_SIZE;  // ESP-IDF: ESP_ERR_NVS_INVALID_LENGTH; callers treat both as failure
    memcpy(out, e->data, e->len);
    *len = e->len;
    return ESP_OK;
}

esp_err_t nvs_set_blob(nvs_handle_t h, const char *key, const void *data, size_t len)
{
    handle_t *p = handle(h);
    if (!p || p->mode != NVS_READWRITE) return ESP_ERR_INVALID_ARG;
    if (s_fail_writes) return ESP_ERR_NVS_NOT_ENOUGH_SPACE;
    if (strlen(key) >= sizeof s_e[0].key) return ESP_ERR_NVS_KEY_TOO_LONG;  // as nvs_page.cpp: 15 characters at most
    entry_t *e = find_entry(p->ns, key);
    if (!e) {
        for (int i = 0; i < MAX_ENTRIES && !e; i++)
            if (!s_e[i].used) e = &s_e[i];
        if (!e) return ESP_ERR_NVS_NOT_ENOUGH_SPACE;
        memset(e, 0, sizeof *e);
        e->used = true;
        strncpy(e->part, s_ns[p->ns].part, sizeof e->part - 1);
        strncpy(e->ns, s_ns[p->ns].ns, sizeof e->ns - 1);
        strncpy(e->key, key, sizeof e->key - 1);
    }
    free(e->data);
    e->data = malloc(len ? len : 1);
    memcpy(e->data, data, len);
    e->len = len;
    s_writes++;
    return ESP_OK;
}

esp_err_t nvs_erase_key(nvs_handle_t h, const char *key)
{
    handle_t *p = handle(h);
    if (!p || p->mode != NVS_READWRITE) return ESP_ERR_INVALID_ARG;
    entry_t *e = find_entry(p->ns, key);
    if (!e) return ESP_ERR_NVS_NOT_FOUND;
    free(e->data);
    memset(e, 0, sizeof *e);
    s_writes++;
    return ESP_OK;
}

esp_err_t nvs_commit(nvs_handle_t h) { return handle(h) ? ESP_OK : ESP_ERR_INVALID_ARG; }

static bool matches(const struct fake_nvs_it *it, const entry_t *e)
{
    return e->used && !strcmp(e->part, it->part) && (it->any_ns || !strcmp(e->ns, it->ns));
}

static esp_err_t advance(nvs_iterator_t *it, int from)
{
    for (int i = from; i < MAX_ENTRIES; i++)
        if (matches(*it, &s_e[i])) {
            (*it)->idx = i;
            return ESP_OK;
        }
    free(*it);
    *it = NULL;
    return ESP_ERR_NVS_NOT_FOUND;
}

esp_err_t nvs_entry_find(const char *part, const char *ns, nvs_type_t type, nvs_iterator_t *out)
{
    s_reads++;
    struct fake_nvs_it *it = calloc(1, sizeof *it);
    strncpy(it->part, part, sizeof it->part - 1);
    it->any_ns = ns == NULL;
    if (ns) strncpy(it->ns, ns, sizeof it->ns - 1);
    *out = it;
    return advance(out, 0);  // every fake entry is a blob, so type doesn't narrow it
}

esp_err_t nvs_entry_next(nvs_iterator_t *it)
{
    if (!it || !*it) return ESP_ERR_INVALID_ARG;
    return advance(it, (*it)->idx + 1);
}

esp_err_t nvs_entry_info(nvs_iterator_t it, nvs_entry_info_t *out)
{
    if (!it) return ESP_ERR_INVALID_ARG;
    memset(out, 0, sizeof *out);
    strncpy(out->namespace_name, s_e[it->idx].ns, sizeof out->namespace_name - 1);
    strncpy(out->key, s_e[it->idx].key, sizeof out->key - 1);
    out->type = NVS_TYPE_BLOB;
    return ESP_OK;
}

void nvs_release_iterator(nvs_iterator_t it) { free(it); }
