/* SPDX-License-Identifier: MIT */
#include "wifi_events.h"
#include "tinytest.h"

static void test_scan_retains_disconnect(void)
{
    atomic_uint pending = 0;
    wifi_plan_event_t event;
    wifi_plan_t plan;
    wifi_plan_init(&plan, true, false);
    wifi_plan_step(&plan, WIFI_EVENT_STARTED, 0);
    wifi_plan_step(&plan, WIFI_EVENT_CONNECTED, 1000);

    /* A scan prevents consumption while repeated presses request the radio. */
    for (int press = 0; press < 100; press++) {
        wifi_events_post(&pending, RADIO_ON_ASKED);
    }
    wifi_events_post(&pending, WIFI_EVENT_DISCONNECTED);
    for (int press = 0; press < 100; press++) {
        wifi_events_post(&pending, RADIO_ON_ASKED);
    }
    CHECK(wifi_events_take(&pending, &event) && event == WIFI_EVENT_DISCONNECTED);
    wifi_plan_actions_t actions = wifi_plan_step(&plan, event, 2000);
    CHECK(actions.connect && actions.rescue_in_ms == 120000);
    CHECK(plan.phase == WIFI_PHASE_CONNECTING);
    CHECK(wifi_events_take(&pending, &event) && event == RADIO_ON_ASKED);
    CHECK(!wifi_events_take(&pending, &event));
}

static void test_latest_state_wins(void)
{
    atomic_uint pending = 0;
    wifi_plan_event_t event;
    wifi_events_post(&pending, WIFI_EVENT_STARTED);
    wifi_events_post(&pending, WIFI_EVENT_DISCONNECTED);
    wifi_events_post(&pending, WIFI_EVENT_CONNECTED);
    wifi_events_post(&pending, RADIO_OFF_ASKED);
    wifi_events_post(&pending, RADIO_ON_ASKED);
    wifi_events_post(&pending, WIFI_EVENT_RETRY_DUE);
    wifi_events_post(&pending, WIFI_EVENT_RESCUE_DUE);
    CHECK(wifi_events_take(&pending, &event) && event == WIFI_EVENT_STARTED);
    CHECK(wifi_events_take(&pending, &event) && event == WIFI_EVENT_CONNECTED);
    CHECK(wifi_events_take(&pending, &event) && event == WIFI_EVENT_RESCUE_DUE);
    CHECK(wifi_events_take(&pending, &event) && event == WIFI_EVENT_RETRY_DUE);
    CHECK(wifi_events_take(&pending, &event) && event == RADIO_ON_ASKED);
    CHECK(!wifi_events_take(&pending, &event));

    wifi_events_post(&pending, WIFI_EVENT_CONNECTED);
    wifi_events_post(&pending, WIFI_EVENT_DISCONNECTED);
    wifi_events_post(&pending, RADIO_ON_ASKED);
    wifi_events_post(&pending, RADIO_OFF_ASKED);
    CHECK(wifi_events_take(&pending, &event) && event == WIFI_EVENT_DISCONNECTED);
    CHECK(wifi_events_take(&pending, &event) && event == RADIO_OFF_ASKED);
    CHECK(!wifi_events_take(&pending, &event));
}

int main(void)
{
    test_scan_retains_disconnect();
    test_latest_state_wins();
    REPORT();
}
