/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "wifi_search.h"
#include "tinytest.h"

int main(void)
{
    /* What says the point would not let the device in, and what does not. */
    CHECK(wifi_search_point_refused(false, 2));    /* authentication expired: no answer */
    CHECK(wifi_search_point_refused(false, 4));    /* association never answered */
    CHECK(wifi_search_point_refused(false, 15));   /* four-way handshake timed out */
    CHECK(wifi_search_point_refused(false, 211));  /* a pinned point of another protection */
    CHECK(!wifi_search_point_refused(false, 8));   /* the device left */
    CHECK(!wifi_search_point_refused(false, 201)); /* no access point of the name at all */
    CHECK(!wifi_search_point_refused(true, 4));    /* inactivity after joining */
    CHECK(!wifi_search_point_refused(true, 200));  /* beacons lost after joining */

    wifi_avoided_t avoided = { 0 };
    const uint8_t node[6] = { 0x50, 0xff, 0x20, 0xc1, 0x8b, 0xf5 };
    const uint8_t router[6] = { 0xd4, 0x9c, 0x53, 0x00, 0x36, 0xc8 };
    const uint8_t none[6] = { 0 };
    wifi_avoided_add(&avoided, node);
    wifi_avoided_add(&avoided, node);
    wifi_avoided_add(&avoided, none); /* a disconnect without an address names no point */
    CHECK(avoided.count == 1);
    CHECK(wifi_avoided_has(&avoided, node));
    CHECK(!wifi_avoided_has(&avoided, router));

    /* The node that ignores the device is stronger; the router is picked. */
    wifi_point_t points[] = {
        { .bssid = { 0x50, 0xff, 0x20, 0xc1, 0x8b, 0xf5 }, .channel = 6, .rssi = -60, .secured = true },
        { .bssid = { 0xd4, 0x9c, 0x53, 0x00, 0x36, 0xc8 }, .channel = 11, .rssi = -64, .secured = true },
        { .bssid = { 0x02, 0, 0, 0, 0, 1 }, .channel = 1, .rssi = -40, .secured = false },
    };
    CHECK(wifi_search_pick(points, 3, &avoided, true) == 1);
    wifi_avoided_t nothing = { 0 };
    CHECK(wifi_search_pick(points, 3, &nothing, true) == 0); /* the strongest secured one */
    CHECK(wifi_search_pick(points, 3, &nothing, false) == 2); /* no password: the open one */
    wifi_avoided_add(&avoided, router);
    CHECK(wifi_search_pick(points, 3, &avoided, true) == -1); /* the open point never stands in */
    CHECK(wifi_search_pick(points, 0, &nothing, true) == -1);

    /* A full table forgets the oldest first. */
    wifi_avoided_t full = { 0 };
    for (uint8_t i = 1; i <= WIFI_SEARCH_AVOIDED_MAX + 1; i++) {
        const uint8_t point[6] = { i, 0, 0, 0, 0, 1 };
        wifi_avoided_add(&full, point);
    }
    const uint8_t first[6] = { 1, 0, 0, 0, 0, 1 }, last[6] = { WIFI_SEARCH_AVOIDED_MAX + 1, 0, 0, 0, 0, 1 };
    CHECK(full.count == WIFI_SEARCH_AVOIDED_MAX);
    CHECK(!wifi_avoided_has(&full, first));
    CHECK(wifi_avoided_has(&full, last));
    wifi_avoided_clear(&full);
    CHECK(full.count == 0 && !wifi_avoided_has(&full, last));
    REPORT();
}
