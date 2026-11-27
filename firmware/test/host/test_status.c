/* SPDX-License-Identifier: MIT */
#include "tinytest.h"
#include "status.h"
#include "settings.h"
#include "events.h"
#include "deadlines.h"
#include "wifi.h"
#include "wifi_status.h"
#include "net_time.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static wifi_status_t network;
static int changes;
static bool mutex;

SemaphoreHandle_t xSemaphoreCreateMutex(void) { return &mutex; }
int xSemaphoreTake(SemaphoreHandle_t lock, unsigned wait)
{
    (void)wait;
    CHECK(!*(bool *)lock);
    *(bool *)lock = true;
    return pdTRUE;
}
void xSemaphoreGive(SemaphoreHandle_t lock) { *(bool *)lock = false; }
void wifi_get_status(wifi_status_t *status) { *status = network; }
void settings_read(appconf_t *settings) { memset(settings, 0, sizeof *settings); }
void settings_watch(settings_watch_fn watcher) { (void)watcher; }
bool net_time_is_known(void) { return false; }
int64_t net_time_now(void) { return 0; }
bool deadlines_set_at(deadline_kind_t kind, uint32_t owner, int number, int64_t at_ms)
{
    (void)kind; (void)owner; (void)number; (void)at_ms;
    return true;
}
bool events_post(const event_t *event)
{
    CHECK(event->kind == EVENT_STATUS_CHANGED);
    changes++;
    return true;
}

static void expect_connection(bool connected, int expected_changes)
{
    status_check();
    status_t status;
    status_read(&status);
    CHECK(status.wifi_connected == connected);
    CHECK(changes == expected_changes);
}

static void begin_attempt(wifi_plan_t *plan, int64_t now_ms)
{
    wifi_plan_init(plan, true, false);
    wifi_plan_step(plan, WIFI_EVENT_STARTED, now_ms);
    wifi_status_update(&network, plan->phase, false);
}

static void receive_address(wifi_plan_t *plan, int64_t now_ms)
{
    strcpy(network.address, "192.0.2.1");
    wifi_plan_step(plan, WIFI_EVENT_CONNECTED, now_ms);
    wifi_status_update(&network, plan->phase, false);
}

static void test_network_up_generation(void)
{
    wifi_status_t status = {0};
    CHECK(wifi_status_update(&status, WIFI_PHASE_ONLINE, false));
    CHECK(status.connection_generation == 1);
    wifi_status_update(&status, WIFI_PHASE_ONLINE, true);
    wifi_status_update(&status, WIFI_PHASE_CONNECTING, false);
    wifi_status_update(&status, WIFI_PHASE_ONLINE, false);
    CHECK(status.connection_generation == 1); /* deliberate radio cycling */
    wifi_status_lost(&status);
    wifi_status_update(&status, WIFI_PHASE_CONNECTING, false);
    wifi_status_update(&status, WIFI_PHASE_ONLINE, false);
    CHECK(status.connection_generation == 2); /* genuine recovery with radio awake */
    wifi_status_update(&status, WIFI_PHASE_ONLINE, true);
    wifi_status_update(&status, WIFI_PHASE_CONNECTING, false);
    wifi_status_lost(&status); /* failed attempt after waking the radio */
    wifi_status_update(&status, WIFI_PHASE_CONNECTING, true);
    wifi_status_update(&status, WIFI_PHASE_CONNECTING, false);
    wifi_status_update(&status, WIFI_PHASE_ONLINE, false);
    CHECK(status.connection_generation == 3);
}

int main(void)
{
    test_network_up_generation();
    wifi_plan_t plan;
    begin_attempt(&plan, 0);
    status_start();
    expect_connection(false, 0);
    receive_address(&plan, 1000);
    expect_connection(true, 1);

    /* Sleeping the radio preserves success without an active address. */
    wifi_status_update(&network, plan.phase, true);
    CHECK(network.address[0] == '\0');
    expect_connection(true, 1);

    /* A wake associates but never gets an address or a disconnect event.
     * After the poller's 20-second wait it gives the radio back. */
    begin_attempt(&plan, 300000);
    expect_connection(false, 2);
    CHECK(plan.phase == WIFI_PHASE_CONNECTING);
    CHECK(plan.failed_attempts == 0);
    CHECK(network.address[0] == '\0');
    wifi_status_update(&network, plan.phase, true);
    expect_connection(false, 2);

    /* Repeated sleep publication must keep the latest attempt's outcome. */
    wifi_status_update(&network, plan.phase, true);
    expect_connection(false, 2);
    begin_attempt(&plan, 620001);
    expect_connection(false, 2);
    receive_address(&plan, 621001);
    expect_connection(true, 3);

    /* A real disconnect also remains crossed out during sleep. */
    wifi_plan_step(&plan, WIFI_EVENT_DISCONNECTED, 622001);
    wifi_status_update(&network, plan.phase, false);
    expect_connection(false, 4);
    wifi_status_update(&network, plan.phase, true);
    expect_connection(false, 4);
    wifi_status_update(&network, WIFI_PHASE_SETUP, false);
    expect_connection(false, 4);
    wifi_status_update(&network, WIFI_PHASE_OFF, false);
    expect_connection(false, 4);
    REPORT();
}
