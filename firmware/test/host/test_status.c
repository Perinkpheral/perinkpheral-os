/* SPDX-License-Identifier: MIT */
#include "tinytest.h"
#include "status.h"
#include "settings.h"
#include "events.h"
#include "deadlines.h"
#include "wifi.h"
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

int main(void)
{
    network.phase = WIFI_PHASE_CONNECTING;
    status_start();
    expect_connection(false, 0);
    network.phase = WIFI_PHASE_ONLINE;
    strcpy(network.address, "192.0.2.1");
    network.was_reached = true;
    expect_connection(true, 1);

    /* Sleeping the radio preserves success without an active address. */
    network.is_radio_off = true;
    network.phase = WIFI_PHASE_CONNECTING;
    network.address[0] = '\0';
    expect_connection(true, 1);
    network.is_radio_off = false;
    expect_connection(false, 2);
    network.phase = WIFI_PHASE_ONLINE;
    strcpy(network.address, "192.0.2.1");
    expect_connection(true, 3);

    /* A lost connection, and a failed attempt followed by sleep, stay crossed out. */
    network.phase = WIFI_PHASE_CONNECTING;
    network.address[0] = '\0';
    network.was_reached = false;
    expect_connection(false, 4);
    network.is_radio_off = true;
    expect_connection(false, 4);
    network.is_radio_off = false;
    network.phase = WIFI_PHASE_SETUP;
    expect_connection(false, 4);
    network.phase = WIFI_PHASE_OFF;
    expect_connection(false, 4);
    REPORT();
}
