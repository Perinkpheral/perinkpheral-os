/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WIFI_SEARCH_BSSID_BYTES 6
#define WIFI_SEARCH_AVOIDED_MAX 4

/* Access points of the network that would not take the device - a mesh node with a node
 * restriction, one that never answers - kept until the device is on the network again
 * (2026-11-22: a node restriction kept the device off the network until probation gave up,
 * every attempt going to the node that ignored it). */
typedef struct {
    uint8_t bssid[WIFI_SEARCH_AVOIDED_MAX][WIFI_SEARCH_BSSID_BYTES];
    uint8_t count;
    uint8_t next; /* the oldest, replaced when the table is full */
} wifi_avoided_t;

/* An access point carrying the network's name, as a scan found it. */
typedef struct {
    uint8_t bssid[WIFI_SEARCH_BSSID_BYTES];
    uint8_t channel;
    int8_t  rssi;
    bool    secured; /* it asks for a password */
} wifi_point_t;

/* Whether a disconnect means this access point would not let the device in: it came before
 * the device had joined it - authentication, association or the handshake never finished,
 * whatever the reason given - and names a point. One lost after joining is not. */
bool wifi_search_point_refused(bool had_joined, uint8_t reason);

void wifi_avoided_add(wifi_avoided_t *avoided, const uint8_t bssid[WIFI_SEARCH_BSSID_BYTES]);
bool wifi_avoided_has(const wifi_avoided_t *avoided, const uint8_t bssid[WIFI_SEARCH_BSSID_BYTES]);
void wifi_avoided_clear(wifi_avoided_t *avoided);

/* The strongest of the points not avoided whose protection matches the credentials (a
 * password: a secured point; none: an open one); -1 when there is none. */
int wifi_search_pick(const wifi_point_t *points, int count, const wifi_avoided_t *avoided,
                     bool has_password);

#ifdef __cplusplus
}
#endif
