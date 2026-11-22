/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "wifi_plan.h"

#include "tinytest.h"

static wifi_plan_t plan;

static wifi_plan_actions_t step(wifi_plan_event_t event, int64_t now_ms)
{
    return wifi_plan_step(&plan, event, now_ms);
}

static void test_no_network_is_setup(void)
{
    wifi_plan_init(&plan, false, false);
    wifi_plan_actions_t actions = step(WIFI_EVENT_STARTED, 0);
    CHECK(actions.start_access_point && !actions.connect && actions.retry_in_ms == 0);
    CHECK(plan.phase == WIFI_PHASE_SETUP);
    CHECK(wifi_phase_has_access_point(plan.phase));

    /* stray events change nothing and start nothing a second time */
    actions = step(WIFI_EVENT_DISCONNECTED, 1000);
    CHECK(!actions.start_access_point && !actions.connect && actions.retry_in_ms == 0);
    actions = step(WIFI_EVENT_RETRY_DUE, 2000);
    CHECK(!actions.connect);
}

static void test_off_does_nothing(void)
{
    wifi_plan_init(&plan, false, true);
    CHECK(plan.phase == WIFI_PHASE_OFF);
    CHECK(!wifi_phase_has_radio(plan.phase));
    CHECK(!wifi_phase_has_access_point(plan.phase));

    /* no radio is started, and nothing it could say raises anything */
    const wifi_plan_event_t events[] = { WIFI_EVENT_STARTED, WIFI_EVENT_DISCONNECTED,
                                         WIFI_EVENT_RETRY_DUE, WIFI_EVENT_CONNECTED,
                                         WIFI_EVENT_RESCUE_DUE };
    int64_t now = 0;
    for (unsigned i = 0; i < sizeof events / sizeof events[0]; i++) {
        /* past the two minutes that bring the rescue access point up */
        now += 200 * 1000;
        wifi_plan_actions_t actions = step(events[i], now);
        CHECK(!actions.start_access_point && !actions.stop_access_point && !actions.connect &&
              actions.retry_in_ms == 0 && actions.rescue_in_ms == 0);
        CHECK(plan.phase == WIFI_PHASE_OFF);
    }
}

static void test_off_can_return_to_setup(void)
{
    wifi_plan_init(&plan, false, true);
    CHECK(!step(WIFI_EVENT_STARTED, 0).start_access_point);
    wifi_plan_init(&plan, false, false);
    CHECK(step(WIFI_EVENT_STARTED, 0).start_access_point);
    CHECK(plan.phase == WIFI_PHASE_SETUP);
}

static void test_off_gives_way_to_a_network(void)
{
    /* stored by a firmware that knew no such state: the network comes first */
    wifi_plan_init(&plan, true, true);
    CHECK(wifi_phase_has_radio(plan.phase));
    CHECK(step(WIFI_EVENT_STARTED, 0).connect);
    CHECK(plan.phase == WIFI_PHASE_CONNECTING);

    /* and every other phase has its radio */
    wifi_plan_init(&plan, false, false);
    CHECK(wifi_phase_has_radio(plan.phase));
    CHECK(wifi_phase_has_radio(WIFI_PHASE_ONLINE) && wifi_phase_has_radio(WIFI_PHASE_RESCUE));
}

static void test_connects_at_once(void)
{
    wifi_plan_init(&plan, true, false);
    wifi_plan_actions_t actions = step(WIFI_EVENT_STARTED, 0);
    CHECK(actions.connect && !actions.start_access_point);
    CHECK(plan.phase == WIFI_PHASE_CONNECTING);

    actions = step(WIFI_EVENT_CONNECTED, 1500);
    CHECK(plan.phase == WIFI_PHASE_ONLINE);
    CHECK(!actions.stop_access_point); /* there was none to stop */
}

static void test_retries_back_off(void)
{
    wifi_plan_init(&plan, true, false);
    step(WIFI_EVENT_STARTED, 0);

    const uint32_t expected_ms[] = { 2000, 4000, 8000, 16000, 30000, 30000 };
    int64_t now = 0;
    for (int attempt = 0; attempt < 6; attempt++) {
        wifi_plan_actions_t failed = step(WIFI_EVENT_DISCONNECTED, now);
        CHECK(!failed.connect && failed.retry_in_ms == expected_ms[attempt]);
        now += failed.retry_in_ms;
        if (now >= 120 * 1000) {
            break;
        }
        CHECK(step(WIFI_EVENT_RETRY_DUE, now).connect);
    }
}

static void test_rescue_after_two_minutes(void)
{
    wifi_plan_init(&plan, true, false);
    step(WIFI_EVENT_STARTED, 0);

    wifi_plan_actions_t actions = step(WIFI_EVENT_DISCONNECTED, 119 * 1000);
    CHECK(plan.phase == WIFI_PHASE_CONNECTING && !actions.start_access_point);

    actions = step(WIFI_EVENT_DISCONNECTED, 120 * 1000);
    CHECK(plan.phase == WIFI_PHASE_RESCUE);
    CHECK(actions.start_access_point && actions.retry_in_ms == 60 * 1000);

    /* it keeps trying, without starting the access point again */
    CHECK(step(WIFI_EVENT_RETRY_DUE, 180 * 1000).connect);
    actions = step(WIFI_EVENT_DISCONNECTED, 185 * 1000);
    CHECK(!actions.start_access_point && actions.retry_in_ms == 60 * 1000);

    /* the network came back: the access point goes */
    actions = step(WIFI_EVENT_CONNECTED, 250 * 1000);
    CHECK(plan.phase == WIFI_PHASE_ONLINE && actions.stop_access_point);
}

static void test_lost_network_starts_a_fresh_run(void)
{
    wifi_plan_init(&plan, true, false);
    step(WIFI_EVENT_STARTED, 0);
    step(WIFI_EVENT_CONNECTED, 1000);

    /* after a week online a drop is tried again at once... */
    int64_t a_week_ms = 7LL * 24 * 3600 * 1000;
    wifi_plan_actions_t actions = step(WIFI_EVENT_DISCONNECTED, a_week_ms);
    CHECK(actions.connect && plan.phase == WIFI_PHASE_CONNECTING);
    CHECK(actions.retry_in_ms == 0 && !actions.start_access_point);

    /* ...and the two minutes to rescue count from the drop, not from boot */
    actions = step(WIFI_EVENT_DISCONNECTED, a_week_ms + 1000);
    CHECK(plan.phase == WIFI_PHASE_CONNECTING && actions.retry_in_ms == 2000);

    CHECK(step(WIFI_EVENT_RETRY_DUE, a_week_ms + 3000).connect);
    actions = step(WIFI_EVENT_DISCONNECTED, a_week_ms + 4000);
    CHECK(!actions.connect && actions.retry_in_ms == 4000);

    /* A stale deadline must not start another attempt after reconnection. */
    step(WIFI_EVENT_CONNECTED, a_week_ms + 5000);
    CHECK(!step(WIFI_EVENT_RETRY_DUE, a_week_ms + 8000).connect);
}

static void test_rescue_without_dhcp(void)
{
    wifi_plan_init(&plan, true, false);
    wifi_plan_actions_t actions = step(WIFI_EVENT_STARTED, 5000);
    CHECK(actions.rescue_in_ms == 120000);
    CHECK(!step(WIFI_EVENT_RESCUE_DUE, 124999).start_access_point);
    actions = step(WIFI_EVENT_RESCUE_DUE, 125000);
    CHECK(actions.start_access_point && !actions.connect);
    CHECK(plan.phase == WIFI_PHASE_RESCUE);
    CHECK(!step(WIFI_EVENT_RESCUE_DUE, 126000).start_access_point);
    actions = step(WIFI_EVENT_CONNECTED, 130000);
    CHECK(actions.stop_access_point && plan.phase == WIFI_PHASE_ONLINE);
    CHECK(!step(WIFI_EVENT_RESCUE_DUE, 140000).start_access_point);

    actions = step(WIFI_EVENT_DISCONNECTED, 200000);
    CHECK(actions.connect && actions.rescue_in_ms == 120000);
    CHECK(!step(WIFI_EVENT_RESCUE_DUE, 319999).start_access_point);
    CHECK(step(WIFI_EVENT_RESCUE_DUE, 320000).start_access_point);
}

static void test_access_point_start_retries(bool has_network)
{
    wifi_plan_init(&plan, has_network, false);
    wifi_plan_actions_t actions = step(WIFI_EVENT_STARTED, 0);
    int64_t now = has_network ? 120000 : 0;
    if (has_network) actions = step(WIFI_EVENT_RESCUE_DUE, now);
    CHECK(actions.start_access_point);

    const uint32_t pauses[] = { 2000, 4000, 8000, 16000, 30000, 30000 };
    for (unsigned attempt = 0; attempt < sizeof pauses / sizeof pauses[0]; attempt++) {
        /* Either driver operation can fail; station retries must not erase
         * the outstanding access point request or its backoff. */
        CHECK(wifi_plan_access_point_result(&plan, false) == pauses[attempt]);
        step(WIFI_EVENT_DISCONNECTED, now);
        CHECK(step(WIFI_EVENT_RETRY_DUE, now).connect == has_network);
        now += pauses[attempt];
        CHECK(step(WIFI_EVENT_RESCUE_DUE, now).start_access_point);
    }
    CHECK(wifi_plan_access_point_result(&plan, true) == 0);
    CHECK(!step(WIFI_EVENT_RESCUE_DUE, now + 30000).start_access_point);
    CHECK(!step(WIFI_EVENT_DISCONNECTED, now + 60000).start_access_point);
}

static void test_connection_cancels_access_point_retry(void)
{
    wifi_plan_init(&plan, true, false);
    step(WIFI_EVENT_STARTED, 0);
    CHECK(step(WIFI_EVENT_RESCUE_DUE, 120000).start_access_point);
    CHECK(wifi_plan_access_point_result(&plan, false) == 2000);
    CHECK(step(WIFI_EVENT_CONNECTED, 121000).stop_access_point);
    CHECK(!step(WIFI_EVENT_RESCUE_DUE, 122000).start_access_point);
    CHECK(plan.access_point_retry_ms == 0);

    step(WIFI_EVENT_DISCONNECTED, 130000);
    CHECK(step(WIFI_EVENT_RESCUE_DUE, 250000).start_access_point);
    CHECK(wifi_plan_access_point_result(&plan, false) == 2000);
    wifi_plan_init(&plan, false, true);
    CHECK(wifi_plan_access_point_result(&plan, false) == 0);
    CHECK(!step(WIFI_EVENT_RESCUE_DUE, 300000).start_access_point);
}

int main(void)
{
    test_no_network_is_setup();
    test_off_does_nothing();
    test_off_can_return_to_setup();
    test_off_gives_way_to_a_network();
    test_connects_at_once();
    test_retries_back_off();
    test_rescue_after_two_minutes();
    test_lost_network_starts_a_fresh_run();
    test_rescue_without_dhcp();
    test_access_point_start_retries(false);
    test_access_point_start_retries(true);
    test_connection_cancels_access_point_retry();
    REPORT();
}
