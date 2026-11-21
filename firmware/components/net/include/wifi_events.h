/* SPDX-License-Identifier: MIT */
#pragma once

#include <stdatomic.h>
#include "wifi_plan.h"

#define RADIO_OFF_ASKED ((wifi_plan_event_t)(WIFI_EVENT_RETRY_DUE + 1))
#define RADIO_ON_ASKED  ((wifi_plan_event_t)(WIFI_EVENT_RETRY_DUE + 2))

/* Keep the latest state and radio request without crowding out either. */
static inline void wifi_events_post(atomic_uint *pending, wifi_plan_event_t event)
{
    unsigned clear = 0;
    if (event == WIFI_EVENT_CONNECTED || event == WIFI_EVENT_DISCONNECTED) {
        clear = (1u << WIFI_EVENT_CONNECTED) | (1u << WIFI_EVENT_DISCONNECTED);
    } else if (event == RADIO_ON_ASKED || event == RADIO_OFF_ASKED) {
        clear = (1u << RADIO_ON_ASKED) | (1u << RADIO_OFF_ASKED);
    }
    unsigned previous = atomic_load(pending);
    unsigned next;
    do {
        next = (previous & ~clear) | (1u << event);
    } while (!atomic_compare_exchange_weak(pending, &previous, next));
}

static inline bool wifi_events_take(atomic_uint *pending, wifi_plan_event_t *event)
{
    unsigned previous = atomic_load(pending);
    unsigned bit;
    do {
        if (previous == 0) {
            return false;
        }
        bit = previous & (0u - previous);
    } while (!atomic_compare_exchange_weak(pending, &previous, previous & ~bit));
    *event = WIFI_EVENT_STARTED;
    while ((bit >>= 1) != 0) {
        *event = (wifi_plan_event_t)(*event + 1);
    }
    return true;
}
