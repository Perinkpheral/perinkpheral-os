/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "tinytest.h"
#include "../../main/poller.c"
#include "wifi_status.h"

static app_manifest_t installed_apps[3];
static int installed_count;
static esp_app_desc_t running = { .version = "0.1.80" };
static deadline_table_t test_deadlines;
static int64_t clock_ms;
static int sent_method, requests_sent;
static bool abandon_answer, saving_power;
static wifi_plan_t network_plan;
static wifi_status_t network;

const esp_app_desc_t *esp_app_get_description(void) { return &running; }
const app_manifest_t *launcher_installed(int *count)
{
    *count = installed_count;
    return installed_apps;
}
const app_manifest_t *launcher_find(const char *name)
{
    for (int i = 0; i < installed_count; i++) {
        if (strcmp(installed_apps[i].name, name) == 0) return &installed_apps[i];
    }
    return NULL;
}

SemaphoreHandle_t xSemaphoreCreateMutex(void) { static bool mutex; return &mutex; }
SemaphoreHandle_t xSemaphoreCreateBinary(void) { return xSemaphoreCreateMutex(); }
int xSemaphoreTake(SemaphoreHandle_t semaphore, unsigned wait)
{
    (void)semaphore; (void)wait;
    return pdTRUE;
}
void xSemaphoreGive(SemaphoreHandle_t semaphore) { (void)semaphore; }
BaseType_t xTaskCreate(void (*task)(void *), const char *name, unsigned stack,
                       void *argument, unsigned priority, TaskHandle_t *handle)
{
    (void)task; (void)name; (void)stack; (void)argument; (void)priority;
    *handle = NULL;
    return pdPASS;
}
void xTaskNotifyGive(TaskHandle_t task) { (void)task; }
unsigned ulTaskNotifyTake(int clear, TickType_t timeout)
{
    (void)clear; (void)timeout;
    return 0;
}
int64_t esp_timer_get_time(void) { return clock_ms * US_PER_MS; }
bool memory_has(memory_need_t need) { (void)need; return true; }
bool wifi_is_online(void) { return network.phase == WIFI_PHASE_ONLINE; }
void wifi_get_status(wifi_status_t *status) { *status = network; }
bool power_is_saving(void) { return saving_power; }
void power_hold_radio(void)
{
    wifi_plan_init(&network_plan, true, false);
    wifi_plan_step(&network_plan, WIFI_EVENT_STARTED, clock_ms);
    wifi_status_update(&network, network_plan.phase, false);
}
void power_release_radio(void)
{
    wifi_status_update(&network, network_plan.phase, true);
}
void settings_watch(settings_watch_fn watcher) { (void)watcher; }
bool events_is_host_loop(void) { return true; }
void events_call_patiently(events_call_fn run, void *argument) { run(argument); }
void host_deliver(const char *name, const app_delivery_t *delivery)
{
    (void)name; (void)delivery;
}
char *app_store_read_manifest(const char *root, const char *name)
{
    (void)root; (void)name;
    return NULL;
}
uint32_t app_kv_poll_pace(const char *name, int poll) { (void)name; (void)poll; return 0; }
bool app_kv_set_poll_pace(const char *name, int poll, uint32_t seconds)
{
    (void)name; (void)poll; (void)seconds;
    return true;
}
int32_t app_kv_get(const char *name, const char *key, void *value, int32_t capacity)
{
    (void)name; (void)key; (void)value; (void)capacity;
    return HOST_ERR_NOT_FOUND;
}

bool deadlines_set_in(deadline_kind_t kind, uint32_t owner, int number, int64_t in_ms)
{
    return deadline_table_set(&test_deadlines, (deadline_id_t){ kind, owner, number },
                              clock_ms + in_ms, true);
}
bool deadlines_has(deadline_kind_t kind, uint32_t owner, int number)
{
    return deadline_table_when(&test_deadlines, (deadline_id_t){ kind, owner, number }) != DEADLINE_NONE;
}
void deadlines_cancel(deadline_kind_t kind, uint32_t owner, int number)
{
    deadline_table_cancel(&test_deadlines, (deadline_id_t){ kind, owner, number });
}
void deadlines_keep_only(deadline_kind_t kind, const uint32_t *owners, int count)
{
    deadline_table_keep_only(&test_deadlines, kind, owners, count);
}

void poller_net_start(const poller_net_host_t *host) { (void)host; }
bool poller_net_setting_of(void *context, const char *key, char *value, size_t size)
{
    (void)context; (void)key; (void)value; (void)size;
    return false;
}
int poller_net_poll(const char *name, int poll, int item, int *method)
{
    (void)item;
    *method = sent_method;
    requests_sent++;
    if (abandon_answer) poller_answer_was_abandoned(name, poll);
    return 0;
}
void poller_net_poll_not_made(const char *name, int poll, int why)
{
    (void)name; (void)poll; (void)why;
}
void poller_net_request(const char *name, const poller_ask_t *ask) { (void)name; (void)ask; }
void poller_net_tell_failure(const char *name, int number, int why)
{
    (void)name; (void)number; (void)why;
}
bool poller_net_fetch(const char *url, size_t max_bytes, poller_net_take_fn take, void *context, int *status)
{
    (void)url; (void)max_bytes; (void)take; (void)context; (void)status;
    return false;
}

static void reset_poller(void)
{
    request_queue_init(&s_queue);
    poll_schedule_init(&s_schedule);
    memset(&s_rounds, 0, sizeof s_rounds);
    deadline_table_init(&test_deadlines);
    memset(&s_abandoned, 0, sizeof s_abandoned);
    memset(installed_apps, 0, sizeof installed_apps);
    installed_count = 1;
    strcpy(installed_apps[0].name, "probe");
    installed_apps[0].id = 1;
    installed_apps[0].poll_count = 1;
    installed_apps[0].poll_every_s[0] = 3600;
    clock_ms = 0;
    requests_sent = 0;
    abandon_answer = false;
    saving_power = false;
    network = (wifi_status_t){ .phase = WIFI_PHASE_ONLINE };
    s_lock = xSemaphoreCreateMutex();
}

static void advance_to(int64_t now)
{
    clock_ms = now;
    deadline_id_t due;
    while (deadline_table_take_due(&test_deadlines, now, &due)) {
        CHECK(due.kind == DEADLINE_POLL);
        poller_poll_is_due(due.owner, due.number);
    }
}

static void test_abandoned_methods(void)
{
    const int methods[] = { HOST_HTTP_GET, HOST_HTTP_HEAD, HOST_HTTP_POST, HOST_HTTP_PUT,
        HOST_HTTP_DELETE, HOST_HTTP_PATCH, HOST_HTTP_PROPFIND, HOST_HTTP_REPORT };
    for (size_t i = 0; i < sizeof methods / sizeof methods[0]; i++) {
        reset_poller();
        sent_method = methods[i];
        abandon_answer = true;
        CHECK(schedule_installed() == 1);
        advance_to(0);
        CHECK(run_wanted_poll());
        bool may_repeat = sent_method == HOST_HTTP_GET || sent_method == HOST_HTTP_HEAD;
        int64_t expected = may_repeat ? 60000 : 3600000;
        CHECK(deadline_table_when(&test_deadlines, (deadline_id_t){ DEADLINE_POLL, 1, 0 }) == expected);
        advance_to(60000);
        CHECK(run_wanted_poll() == may_repeat);
        CHECK(requests_sent == (may_repeat ? 2 : 1));
        if (!may_repeat) {
            advance_to(3600000);
            CHECK(run_wanted_poll());
            CHECK(requests_sent == 2);
        }
    }
    reset_poller();
    installed_apps[0].poll_every_s[0] = 30;
    sent_method = HOST_HTTP_GET;
    abandon_answer = true;
    CHECK(schedule_installed() == 1);
    advance_to(0);
    CHECK(run_wanted_poll());
    CHECK(deadline_table_when(&test_deadlines, (deadline_id_t){ DEADLINE_POLL, 1, 0 }) == 30000);
}

static void test_incompatible_apps(void)
{
    reset_poller();
    installed_count = 3;
    installed_apps[1] = installed_apps[0];
    installed_apps[1].id = 2;
    strcpy(installed_apps[1].name, "future");
    CHECK(app_version_number("0.1.81", &installed_apps[1].firmware));
    installed_apps[2] = installed_apps[0];
    installed_apps[2].id = 3;
    strcpy(installed_apps[2].name, "current");
    CHECK(app_version_number("0.1.80", &installed_apps[2].firmware));
    CHECK(schedule_installed() == 2);
    CHECK(poll_schedule_has(&s_schedule, 1, 0));
    CHECK(!poll_schedule_has(&s_schedule, 2, 0));
    CHECK(poll_schedule_has(&s_schedule, 3, 0));
    CHECK(!deadlines_has(DEADLINE_POLL, 2, 0));
    CHECK(!poller_poll_now("future", 0));
    poller_network_is_back();
    sent_method = HOST_HTTP_GET;
    while (run_wanted_poll()) { }
    CHECK(requests_sent == 2);
    /* A downgrade/reload also removes previously scheduled incompatible polls. */
    CHECK(app_version_number("0.1.81", &installed_apps[0].firmware));
    poller_reload();
    CHECK(s_schedule.count == 1);
    CHECK(!poll_schedule_has(&s_schedule, 1, 0));
    CHECK(!deadlines_has(DEADLINE_POLL, 1, 0));
    poller_network_is_back();
    while (run_wanted_poll()) { }
    CHECK(requests_sent == 3);
}

static void test_interval_below_saving_floor(void)
{
    reset_poller();
    saving_power = true;
    installed_apps[0].poll_every_s[0] = 10;
    sent_method = HOST_HTTP_GET;
    CHECK(schedule_installed() == 1);
    advance_to(0);
    CHECK(run_wanted_poll());
    deadline_id_t poll = { DEADLINE_POLL, 1, 0 };
    CHECK(deadline_table_when(&test_deadlines, poll) == 300000);
    clock_ms = 1000;
    CHECK(poller_set_interval("probe", 0, 20) == HOST_OK);
    CHECK(poll_schedule_pace_ms(&s_schedule, 1, 0) == 300000);
    CHECK(deadline_table_when(&test_deadlines, poll) == 301000);
    clock_ms = 2000;
    CHECK(poller_set_interval("probe", 0, 20) == HOST_OK);
    CHECK(deadline_table_when(&test_deadlines, poll) == 301000);
}

static void test_wake_without_address(void)
{
    reset_poller();
    saving_power = true;
    sent_method = HOST_HTTP_GET;
    CHECK(schedule_installed() == 1);
    advance_to(0);
    keep_radio_for_work();
    CHECK(s_is_holding_radio);
    wifi_plan_step(&network_plan, WIFI_EVENT_CONNECTED, 1000);
    strcpy(network.address, "192.0.2.1");
    wifi_status_update(&network, network_plan.phase, false);
    CHECK(run_wanted_poll());
    keep_radio_for_work();
    CHECK(!s_is_holding_radio);
    CHECK(network.is_radio_off && network.was_reached);

    clock_ms = 300000;
    CHECK(poller_poll_now("probe", 0));
    keep_radio_for_work();
    CHECK(s_is_holding_radio);
    CHECK(!network.is_radio_off);
    /* Association alone supplies neither an address nor a disconnect event. */
    CHECK(network_plan.phase == WIFI_PHASE_CONNECTING);
    CHECK(network_plan.failed_attempts == 0);
    CHECK(network.address[0] == '\0');
    clock_ms += RADIO_WAIT_MS;
    keep_radio_for_work();
    CHECK(s_is_holding_radio);
    clock_ms++;
    keep_radio_for_work();
    CHECK(!s_is_holding_radio);
    CHECK(network.is_radio_off);
    CHECK(!network.was_reached);
    CHECK(requests_sent == 1);
}

int main(void)
{
    test_abandoned_methods();
    test_incompatible_apps();
    test_interval_below_saving_floor();
    test_wake_without_address();
    REPORT();
}
