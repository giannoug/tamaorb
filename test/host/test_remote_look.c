// SPDX-License-Identifier: GPL-3.0-or-later
#include <string.h>
#include "unity.h"
#include "remote.h"
#include "ui_internal.h"
#include "persist.h"

void setUp(void) {}
void tearDown(void) {}

static rm_look_t base(void)
{
    rm_look_t l;
    memset(&l, 0, sizeof l);
    l.model = TC_MODEL_P1;
    l.phase = RM_PHASE_RUN;
    return l;
}

static void test_text_exact(void)
{
    rm_look_t l = base();
    l.frame.rows[0] = 1u;
    l.frame.rows[15] = 1u << 31;
    l.frame.icons = (1 << 0) | (1 << 7);
    l.buzzer = true;
    l.seq = 42;
    char buf[1024];
    size_t n = rm_look_text(&l, buf, sizeof buf);
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL(n, strlen(buf));
    TEST_ASSERT_EQUAL_STRING_LEN("#......", buf, 7);
    TEST_ASSERT_EQUAL_STRING_LEN(".#\n", buf + 15 * 33 + 30, 3);
    TEST_ASSERT_NOT_NULL(strstr(buf, "icons: food attention\n"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "buzzer: on\n"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "phase: run\n"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "frame: 42\n"));
}

static void test_text_none_and_boot(void)
{
    rm_look_t l = base();
    l.phase = RM_PHASE_BOOT;
    char buf[1024];
    rm_look_text(&l, buf, sizeof buf);
    TEST_ASSERT_NOT_NULL(strstr(buf, "icons: none\n"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "buzzer: off\n"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "phase: boot\n"));
}

static void test_text_catchup(void)
{
    rm_look_t l = base();
    l.phase = RM_PHASE_CATCHUP;
    l.catchup_pct = 46;
    char buf[1024];
    rm_look_text(&l, buf, sizeof buf);
    TEST_ASSERT_NOT_NULL(strstr(buf, "phase: catch-up 46%\n"));
}

static void test_text_cap_too_small(void)
{
    rm_look_t l = base();
    char buf[100];
    TEST_ASSERT_EQUAL(0, rm_look_text(&l, buf, sizeof buf));
}

// ---- minimal PNG decoder ----
static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

static uint8_t raw[4096];
static size_t raw_n;

static void decode(const uint8_t *png, size_t n)
{
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    TEST_ASSERT_TRUE(n > 8);
    TEST_ASSERT_EQUAL_MEMORY(sig, png, 8);
    static uint8_t z[4096];
    size_t zn = 0, pos = 8;
    bool seen_ihdr = false, seen_iend = false;
    while (pos + 12 <= n) {
        uint32_t len = be32(png + pos);
        const uint8_t *type = png + pos + 4;
        TEST_ASSERT_TRUE(pos + 12 + len <= n);
        TEST_ASSERT_EQUAL_HEX32(be32(png + pos + 8 + len), ps_crc32(type, 4 + len));
        if (!memcmp(type, "IHDR", 4)) {
            seen_ihdr = true;
            TEST_ASSERT_EQUAL(13, len);
            TEST_ASSERT_EQUAL(128, be32(type + 4));
            TEST_ASSERT_EQUAL(96, be32(type + 8));
            TEST_ASSERT_EQUAL(1, type[12]);
            TEST_ASSERT_EQUAL(0, type[13]);
        } else if (!memcmp(type, "IDAT", 4)) {
            memcpy(z + zn, type + 4, len);
            zn += len;
        } else if (!memcmp(type, "IEND", 4)) {
            seen_iend = true;
        }
        pos += 12 + len;
    }
    TEST_ASSERT_TRUE(seen_ihdr && seen_iend);
    TEST_ASSERT_EQUAL_HEX8(0x78, z[0]);
    TEST_ASSERT_EQUAL_HEX8(0x01, z[1]);
    size_t i = 2;
    raw_n = 0;
    for (;;) {
        uint8_t h = z[i];
        TEST_ASSERT_EQUAL(0, (h >> 1) & 3);
        uint16_t l = z[i + 1] | z[i + 2] << 8, nl = z[i + 3] | z[i + 4] << 8;
        TEST_ASSERT_EQUAL_HEX16(0xFFFF, (uint16_t)(l ^ nl));
        memcpy(raw + raw_n, z + i + 5, l);
        raw_n += l;
        i += 5 + l;
        if (h & 1) break;
    }
    uint32_t a = 1, b = 0;
    for (size_t k = 0; k < raw_n; k++) { a = (a + raw[k]) % 65521; b = (b + a) % 65521; }
    TEST_ASSERT_EQUAL_HEX32(b << 16 | a, be32(z + i));
    TEST_ASSERT_EQUAL(i + 4, zn);
    TEST_ASSERT_EQUAL(96 * 17, raw_n);
}

static int px(int x, int y)  // 1 = white
{
    TEST_ASSERT_EQUAL(0, raw[y * 17]);
    return (raw[y * 17 + 1 + x / 8] >> (7 - x % 8)) & 1;
}

static void test_png_decodes(void)
{
    rm_look_t l = base();
    l.frame.rows[0] = 1u;
    l.frame.icons = 1;
    static uint8_t png[4096];
    size_t n = rm_look_png(&l, png, sizeof png);
    TEST_ASSERT_TRUE(n > 0);
    decode(png, n);
    TEST_ASSERT_EQUAL(0, px(1, 17));
    TEST_ASSERT_EQUAL(1, px(5, 17));
    for (int y = 0; y < 12; y++)
        for (int x = 0; x < 12; x++)
            TEST_ASSERT_EQUAL(UI_ICONS[0][y][x] == '#' ? 0 : 1, px(10 + x, y));
    TEST_ASSERT_EQUAL(1, px(10 + 32 + 3, 5));  // icon 1 unlit
}

static void test_png_bottom_and_p2(void)
{
    rm_look_t l = base();
    l.model = TC_MODEL_P2;
    l.frame.icons = (1 << 3) | (1 << 7);
    static uint8_t png[4096];
    size_t n = rm_look_png(&l, png, sizeof png);
    decode(png, n);
    for (int y = 0; y < 12; y++)
        for (int x = 0; x < 12; x++) {
            TEST_ASSERT_EQUAL(UI_ICON_P2_MEDICINE[y][x] == '#' ? 0 : 1, px(10 + 96 + x, y));
            TEST_ASSERT_EQUAL(UI_ICONS[7][y][x] == '#' ? 0 : 1, px(10 + 96 + x, 84 + y));
        }
}

static void test_png_cap_too_small(void)
{
    rm_look_t l = base();
    uint8_t png[100];
    TEST_ASSERT_EQUAL(0, rm_look_png(&l, png, sizeof png));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_text_exact);
    RUN_TEST(test_text_none_and_boot);
    RUN_TEST(test_text_catchup);
    RUN_TEST(test_text_cap_too_small);
    RUN_TEST(test_png_decodes);
    RUN_TEST(test_png_bottom_and_p2);
    RUN_TEST(test_png_cap_too_small);
    return UNITY_END();
}
