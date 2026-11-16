// SPDX-License-Identifier: GPL-3.0-or-later
#include <string.h>
#include "unity.h"
#include "tzdb.h"

void setUp(void) {}
void tearDown(void) {}

static void test_tzdb_sorted_and_find(void)
{
    TEST_ASSERT_GREATER_THAN(400, tzdb_count());
    for (int i = 1; i < tzdb_count(); i++)
        TEST_ASSERT_TRUE(strcmp(tzdb_at(i - 1)->name, tzdb_at(i)->name) < 0);
    const tzdb_zone_t *z = tzdb_find("Europe/Berlin");
    TEST_ASSERT_NOT_NULL(z);
    TEST_ASSERT_EQUAL_STRING("CET-1CEST,M3.5.0,M10.5.0/3", z->posix);
    TEST_ASSERT_NULL(tzdb_find("Europe/Nowhere"));
    TEST_ASSERT_NULL(tzdb_find("europe/berlin"));
    TEST_ASSERT_NULL(tzdb_find(""));
    TEST_ASSERT_NULL(tzdb_find(NULL));
    TEST_ASSERT_NULL(tzdb_at(-1));
    TEST_ASSERT_NULL(tzdb_at(tzdb_count()));
    TEST_ASSERT_NOT_NULL(tzdb_find(tzdb_at(0)->name));
    TEST_ASSERT_NOT_NULL(tzdb_find(tzdb_at(tzdb_count() - 1)->name));
}

static void test_posix_valid(void)
{
    TEST_ASSERT_TRUE(tzdb_posix_valid("UTC0"));
    TEST_ASSERT_TRUE(tzdb_posix_valid("<+03>-3"));
    TEST_ASSERT_TRUE(tzdb_posix_valid("EST5EDT,M3.2.0,M11.1.0"));
    TEST_ASSERT_TRUE(tzdb_posix_valid("EET-2EEST,M3.5.0/3,M10.5.0/4"));
    TEST_ASSERT_TRUE(tzdb_posix_valid("<-03>3<-02>,M3.5.0/-2,M10.5.0/-1"));
    TEST_ASSERT_TRUE(tzdb_posix_valid("NZST-12NZDT,M9.5.0,M4.1.0/3"));
    TEST_ASSERT_TRUE(tzdb_posix_valid("<+0530>-5:30"));
    TEST_ASSERT_TRUE(tzdb_posix_valid("EST5EDT4,J60,300/2:30"));
    TEST_ASSERT_FALSE(tzdb_posix_valid(""));
    TEST_ASSERT_FALSE(tzdb_posix_valid(NULL));
    TEST_ASSERT_FALSE(tzdb_posix_valid("Mars"));
    TEST_ASSERT_FALSE(tzdb_posix_valid("UTC"));
    TEST_ASSERT_FALSE(tzdb_posix_valid("EST5EDT,"));
    TEST_ASSERT_FALSE(tzdb_posix_valid("EST5EDT,M3.2.0"));
    TEST_ASSERT_FALSE(tzdb_posix_valid("EST5EDT,M13.2.0,M11.1.0"));
    TEST_ASSERT_FALSE(tzdb_posix_valid("EST5EDT,M3.6.0,M11.1.0"));
    TEST_ASSERT_FALSE(tzdb_posix_valid("EST5EDT,M3.2.7,M11.1.0"));
    TEST_ASSERT_FALSE(tzdb_posix_valid("EST5 "));
    TEST_ASSERT_FALSE(tzdb_posix_valid("UTC0x"));
    TEST_ASSERT_FALSE(tzdb_posix_valid("<+0>-3"));
    TEST_ASSERT_FALSE(tzdb_posix_valid("<+03-3"));
    TEST_ASSERT_FALSE(tzdb_posix_valid("\"; rm"));
}

static void test_every_table_entry_is_valid_posix(void)
{
    for (int i = 0; i < tzdb_count(); i++)
        TEST_ASSERT_TRUE_MESSAGE(tzdb_posix_valid(tzdb_at(i)->posix), tzdb_at(i)->name);
}

static void test_regions_unique(void)
{
    const char *r[32];
    int n = tzdb_regions(r, 32);
    TEST_ASSERT_GREATER_THAN(5, n);
    for (int i = 0; i < n; i++) {
        TEST_ASSERT_NULL(strchr(r[i], '/'));
        TEST_ASSERT_TRUE(strcmp(r[i], "Etc") != 0);
        for (int j = 0; j < i; j++) TEST_ASSERT_TRUE(strcmp(r[i], r[j]) != 0);
    }
    int utc = 0, europe = 0;
    for (int i = 0; i < n; i++) {
        utc += strcmp(r[i], "UTC") == 0;
        europe += strcmp(r[i], "Europe") == 0;
    }
    TEST_ASSERT_EQUAL(1, utc);
    TEST_ASSERT_EQUAL(1, europe);
    TEST_ASSERT_EQUAL(3, tzdb_regions(r, 3));
    TEST_ASSERT_EQUAL(0, tzdb_regions(r, 0));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_tzdb_sorted_and_find);
    RUN_TEST(test_posix_valid);
    RUN_TEST(test_every_table_entry_is_valid_posix);
    RUN_TEST(test_regions_unique);
    return UNITY_END();
}
