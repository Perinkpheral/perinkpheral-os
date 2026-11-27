/* SPDX-License-Identifier: MIT */
#include "tinytest.h"
#include <time.h>

static time_t raw_clock;
static time_t test_wall_time(time_t *out)
{
    if (out != NULL) {
        *out = raw_clock;
    }
    return raw_clock;
}

/* ESP-IDF's newlib provides this; unused net_time_start() is discarded here. */
size_t strlcpy(char *out, const char *source, size_t capacity);
#define time test_wall_time
#include "../../components/net/net_time.c"
#undef time

bool deadlines_set_clock(const struct timeval *to, int64_t *moved_ms)
{
    *moved_ms = ((int64_t)to->tv_sec - raw_clock) * 1000;
    raw_clock = to->tv_sec;
    return true;
}

int main(void)
{
    net_time_set_zone("FIX-1");
    const int64_t unknown[] = { HOST_TIME_MIN, -1, 0, HOST_CLOCK_MIN - 1, HOST_CLOCK_MAX + 1 };
    for (size_t index = 0; index < sizeof unknown / sizeof unknown[0]; index++) {
        raw_clock = (time_t)unknown[index];
        CHECK(net_time_now() == 0);
        CHECK(!net_time_is_known());
        CHECK(net_time_zone_offset() == 0);
    }
    const int64_t known[] = { HOST_CLOCK_MIN, HOST_CLOCK_MAX };
    for (size_t index = 0; index < sizeof known / sizeof known[0]; index++) {
        raw_clock = (time_t)known[index];
        CHECK(net_time_now() == known[index] && net_time_is_known());
        CHECK(net_time_zone_offset() == 3600);
    }
    CHECK(net_time_zone_offset_at(HOST_TIME_MIN) == 3600);
    CHECK(net_time_zone_offset_at(-1) == 3600);
    CHECK(net_time_zone_offset_at(0) == 3600);
    CHECK(net_time_zone_offset_at(HOST_TIME_MAX) == 3600);
    CHECK(net_time_zone_offset_at(HOST_TIME_MIN - 1) == 0);
    CHECK(net_time_zone_offset_at(HOST_TIME_MAX + 1) == 0);
    net_time_set_zone("CET-1CEST,M3.5.0/2,M10.5.0/3");
    CHECK(net_time_zone_offset_at(946684800) == 3600); /* 2000-01-01 UTC */
    CHECK(net_time_zone_offset_at(962452800) == 7200); /* 2000-07-01 UTC */
    s_automatic = false;
    CHECK(!net_time_set(HOST_CLOCK_MIN - 1));
    CHECK(!net_time_set(HOST_CLOCK_MAX + 1));
    CHECK(net_time_set(HOST_CLOCK_MIN) && raw_clock == HOST_CLOCK_MIN);
    CHECK(net_time_set(HOST_CLOCK_MAX) && raw_clock == HOST_CLOCK_MAX);
    REPORT();
}
