/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "wifi_search.h"

#include <string.h>

/* esp_wifi_types: NO_AP_FOUND 201 names no point that answered; ASSOC_LEAVE 8 is the
 * device leaving. Listed by number: this file stays pure. */
bool wifi_search_point_refused(bool had_joined, uint8_t reason)
{
    return !had_joined && reason != 201 && reason != 8;
}

bool wifi_avoided_has(const wifi_avoided_t *avoided, const uint8_t bssid[WIFI_SEARCH_BSSID_BYTES])
{
    for (uint8_t i = 0; i < avoided->count; i++) {
        if (memcmp(avoided->bssid[i], bssid, WIFI_SEARCH_BSSID_BYTES) == 0) {
            return true;
        }
    }
    return false;
}

void wifi_avoided_add(wifi_avoided_t *avoided, const uint8_t bssid[WIFI_SEARCH_BSSID_BYTES])
{
    static const uint8_t none[WIFI_SEARCH_BSSID_BYTES] = { 0 };
    if (memcmp(bssid, none, sizeof none) == 0 || wifi_avoided_has(avoided, bssid)) {
        return;
    }
    if (avoided->count < WIFI_SEARCH_AVOIDED_MAX) {
        memcpy(avoided->bssid[avoided->count++], bssid, WIFI_SEARCH_BSSID_BYTES);
        return;
    }
    memcpy(avoided->bssid[avoided->next], bssid, WIFI_SEARCH_BSSID_BYTES);
    avoided->next = (uint8_t)((avoided->next + 1) % WIFI_SEARCH_AVOIDED_MAX);
}

void wifi_avoided_clear(wifi_avoided_t *avoided)
{
    memset(avoided, 0, sizeof *avoided);
}

int wifi_search_pick(const wifi_point_t *points, int count, const wifi_avoided_t *avoided,
                     bool has_password)
{
    int best = -1;
    for (int i = 0; i < count; i++) {
        if (wifi_avoided_has(avoided, points[i].bssid) || points[i].secured != has_password) {
            continue;
        }
        if (best < 0 || points[i].rssi > points[best].rssi) {
            best = i;
        }
    }
    return best;
}
