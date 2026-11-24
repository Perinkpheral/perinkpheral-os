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
static bool transport_failure, reconnect_during_request;
static int round_items, raw_sent, failures_told, notifications, radio_holds;
static int retry_after, round_poll, sent_item, response_ms;
static bool session_memory = true, page_memory = true;
static const char *served_manifest;
static int deliveries, delivered_request, delivered_status;

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
void xTaskNotifyGive(TaskHandle_t task) { (void)task; notifications++; }
unsigned ulTaskNotifyTake(int clear, TickType_t timeout)
{
    (void)clear; (void)timeout;
    return 0;
}
int64_t esp_timer_get_time(void) { return clock_ms * US_PER_MS; }
bool memory_has(memory_need_t need)
{
    if (need == MEMORY_FOR_A_SESSION) return session_memory;
    if (need == MEMORY_FOR_A_PAGE) return page_memory;
    return true;
}
bool wifi_is_online(void) { return network.phase == WIFI_PHASE_ONLINE; }
void wifi_get_status(wifi_status_t *status) { *status = network; }
bool power_is_saving(void) { return saving_power; }
void power_hold_radio(void)
{
    radio_holds++;
    if (network.is_radio_off) {
        wifi_plan_init(&network_plan, true, false);
        wifi_plan_step(&network_plan, WIFI_EVENT_STARTED, clock_ms);
        wifi_status_update(&network, network_plan.phase, false);
    }
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
    CHECK_STR(name, "probe");
    deliveries++;
    delivered_request = delivery->request;
    delivered_status = delivery->http_status;
}
char *app_store_read_manifest(const char *root, const char *name)
{
    (void)root; (void)name;
    return served_manifest != NULL ? strdup(served_manifest) : NULL;
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
int poller_net_poll(const char *name, int poll, int item, int *method, bool *network_failed)
{
    sent_item = item;
    if (retry_after != 0) {
        NET_HOST.retry_after(retry_after);
        retry_after = 0;
    }
    clock_ms += response_ms;
    *method = sent_method;
    *network_failed = transport_failure;
    if (reconnect_during_request) poller_network_is_back();
    requests_sent++;
    if (abandon_answer) poller_answer_was_abandoned(name, poll);
    return round_poll < 0 || poll == round_poll ? round_items : 0;
}
void poller_net_poll_not_made(const char *name, int poll, int why)
{
    (void)name; (void)poll; (void)why;
}
void poller_net_request(const char *name, const poller_ask_t *ask)
{
    if (retry_after != 0) {
        NET_HOST.retry_after(retry_after);
        retry_after = 0;
    }
    clock_ms += response_ms;
    raw_sent++;
    const app_delivery_t answer = { .kind = APP_DELIVERY_PIECE, .request = ask->number,
        .http_status = 200, .flags = HOST_DATA_FIRST | HOST_DATA_LAST };
    NET_HOST.deliver(name, &answer);
}
void poller_net_tell_failure(const char *name, int number, int why)
{
    failures_told++;
    const app_delivery_t answer = { .kind = APP_DELIVERY_PIECE, .request = number,
        .http_status = why, .flags = HOST_DATA_FAILED };
    NET_HOST.deliver(name, &answer);
}
bool poller_net_fetch(const char *url, size_t max_bytes, poller_net_take_fn take, void *context, int *status)
{
    (void)url; (void)max_bytes; (void)take; (void)context; (void)status;
    return false;
}

static void set_network_online(bool online)
{
    network_plan.phase = online ? WIFI_PHASE_ONLINE : WIFI_PHASE_CONNECTING;
    wifi_status_update(&network, network_plan.phase, false);
    if (online) strcpy(network.address, "192.0.2.1");
}

static void reset_poller(void)
{
    request_queue_init(&s_queue);
    poll_schedule_init(&s_schedule);
    memset(&s_rounds, 0, sizeof s_rounds);
    deadline_table_init(&test_deadlines);
    memset(&s_abandoned, 0, sizeof s_abandoned);
    memset(s_cooldowns, 0, sizeof s_cooldowns);
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
    set_network_online(true);
    s_is_holding_radio = false;
    transport_failure = reconnect_during_request = false;
    round_items = raw_sent = failures_told = notifications = radio_holds = 0;
    retry_after = round_poll = response_ms = 0;
    s_network_generation = 0;
    s_memory_looks = 0;
    session_memory = page_memory = true;
    served_manifest = NULL;
    deliveries = delivered_request = delivered_status = 0;
    s_polled_now_at_ms = -POLL_NOW_EVERY_MS;
    sent_method = HOST_HTTP_GET;
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
    advance_to(2000);
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
    CHECK(requests_sent == 2);
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
    power_release_radio();
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
    CHECK(run_wanted_poll());
    /* The user brought the radio up during its retry pause. Work on this
     * existing connection must retain it beyond the user-access window. */
    clock_ms++;
    set_network_online(true);
    poller_network_is_back();
    keep_radio_for_work();
    CHECK(s_is_holding_radio && radio_holds == 3);
    CHECK(run_wanted_poll());
    keep_radio_for_work();
    CHECK(!s_is_holding_radio);
    CHECK(network.is_radio_off && network.was_reached);
}

static void test_reconnect_offline_poll(void)
{
    for (int saving = 0; saving <= 1; saving++) {
        reset_poller();
        saving_power = saving;
        CHECK(schedule_installed() == 1);
        set_network_online(false);
        advance_to(0);
        CHECK(run_wanted_poll());
        CHECK(requests_sent == 0);
        clock_ms = 9000;
        set_network_online(true);
        poller_network_is_back();
        CHECK(run_wanted_poll());
        CHECK(requests_sent == 1);
        CHECK(!run_wanted_poll());
    }
}

static void test_reconnect_keeps_normal_pace(void)
{
    reset_poller();
    CHECK(schedule_installed() == 1);
    advance_to(0);
    CHECK(run_wanted_poll());
    clock_ms = 9000;
    poller_network_is_back();
    CHECK(!run_wanted_poll());
    CHECK(requests_sent == 1);
}

static void test_reconnect_transport_and_races(void)
{
    for (int saving = 0; saving <= 1; saving++) {
        for (int during = 0; during <= 1; during++) {
            reset_poller();
            saving_power = saving;
            CHECK(schedule_installed() == 1);
            advance_to(0);
            transport_failure = true;
            reconnect_during_request = during;
            CHECK(run_wanted_poll());
            CHECK(network.phase == WIFI_PHASE_ONLINE && network.was_reached);
            CHECK_STR(network.address, "192.0.2.1");
            reconnect_during_request = false;
            poller_network_is_back();
            poller_network_is_back();
            transport_failure = false;
            CHECK(run_wanted_poll());
            CHECK(!run_wanted_poll());
            CHECK(requests_sent == 2);
            /* A later connection cannot replay the recovered poll. */
            poller_network_is_back();
            CHECK(!run_wanted_poll());
            CHECK(radio_holds == 0);
        }
    }
}

static void test_reconnect_due_and_cooldown(void)
{
    for (int saving = 0; saving <= 1; saving++) {
        reset_poller();
        saving_power = saving;
        CHECK(schedule_installed() == 1);
        advance_to(0);
        CHECK(run_wanted_poll());
        /* HTTP errors, including 429, are answers rather than transport failures.
         * An application can extend its interval and the connection must honor it. */
        CHECK(poller_set_interval("probe", 0, 7200) == HOST_OK);
        set_network_online(false);
        advance_to(3600000);
        set_network_online(true);
        poller_network_is_back();
        CHECK(!run_wanted_poll());
        set_network_online(false);
        advance_to(7200000);
        set_network_online(true);
        poller_network_is_back();
        poller_network_is_back();
        CHECK(run_wanted_poll());
        CHECK(!run_wanted_poll());
        CHECK(requests_sent == 2);
    }
}

static void test_reconnect_each_and_deferred_poll(void)
{
    reset_poller();
    CHECK(schedule_installed() == 1);
    advance_to(0);
    round_items = 100;
    transport_failure = true;
    CHECK(run_wanted_poll());
    CHECK(!poll_rounds_pending(&s_rounds));
    CHECK(!run_wanted_poll());
    /* A deferred explicit request already promises a retry; reconnect does
     * not bypass that limiter or add a second list round. */
    clock_ms = 100;
    s_polled_now_at_ms = 0;
    CHECK(poller_poll_now("probe", 0));
    poller_network_is_back();
    CHECK(!run_wanted_poll());
    advance_to(1000);
    poller_network_is_back();
    transport_failure = false;
    for (int item = 0; item < round_items; item++) CHECK(run_wanted_poll());
    CHECK(requests_sent == 101);
    CHECK(!run_wanted_poll());
}

static void test_each_cooldown_survives_transport_failure(void)
{
    reset_poller();
    CHECK(schedule_installed() == 1);
    advance_to(0);
    round_items = 5;
    CHECK(run_wanted_poll());
    /* The application lengthens its interval mid-round. A later item
     * loses its connection before the round can finish. */
    clock_ms = 100;
    CHECK(poller_set_interval("probe", 0, 7200) == HOST_OK);
    clock_ms = 200;
    transport_failure = true;
    CHECK(run_wanted_poll());
    poller_network_is_back();
    CHECK(!run_wanted_poll());
    CHECK(deadline_table_when(&test_deadlines, (deadline_id_t){ DEADLINE_POLL, 1, 0 }) == 7200100);
    transport_failure = false;
    advance_to(7200100);
    for (int item = 0; item < round_items; item++) CHECK(run_wanted_poll());
    CHECK(!run_wanted_poll());
    CHECK(requests_sent == 7);
}

static void test_local_failure_keeps_pace(void)
{
    for (int saving = 0; saving <= 1; saving++) {
        reset_poller();
        saving_power = saving;
        CHECK(schedule_installed() == 1);
        advance_to(0);
        session_memory = false;
        for (int look = 1; look < MEMORY_LOOKS_MAX; look++) CHECK(!run_wanted_poll());
        CHECK(run_wanted_poll());
        CHECK(network.phase == WIFI_PHASE_ONLINE && network.was_reached);
        session_memory = true;
        poller_network_is_back();
        CHECK(!run_wanted_poll());
        CHECK(requests_sent == 0);
        advance_to(3600000);
        CHECK(run_wanted_poll());
        CHECK(requests_sent == 1);
    }
}

static void test_reconnect_raw_queue(void)
{
    reset_poller();
    CHECK(schedule_installed() == 1);
    set_network_online(false);
    for (int tag = 0; tag < HOST_HTTP_PENDING_MAX; tag++) {
        CHECK(request_queue_add(&s_queue, 1, "probe", HOST_REQUEST_FIRST + tag,
              HOST_HTTP_GET, "https://example.test", NULL, 0, NULL, 0, clock_ms) == REQUEST_QUEUE_ADDED);
    }
    CHECK(request_queue_add(&s_queue, 1, "probe", HOST_REQUEST_FIRST + HOST_HTTP_PENDING_MAX,
          HOST_HTTP_GET, "https://example.test", NULL, 0, NULL, 0, clock_ms) != REQUEST_QUEUE_ADDED);
    clock_ms = 9000;
    CHECK(!fail_request_without_network());
    set_network_online(true);
    s_task = &network;
    poller_network_is_back();
    CHECK(notifications > 0);
    while (run_pending_request(0)) { }
    CHECK(raw_sent == HOST_HTTP_PENDING_MAX && failures_told == 0);
    CHECK(!has_pending_request());
    s_task = NULL;
    set_network_online(false);
    CHECK(request_queue_add(&s_queue, 1, "probe", HOST_REQUEST_FIRST,
          HOST_HTTP_POST, "https://example.test", NULL, 0, NULL, 0, clock_ms) == REQUEST_QUEUE_ADDED);
    clock_ms += RADIO_WAIT_MS + 1;
    CHECK(fail_request_without_network());
    set_network_online(true);
    poller_network_is_back();
    CHECK(!run_pending_request(0));
    CHECK(failures_told == 1);
}

static void test_request_admission(void)
{
    reset_poller();
    CHECK(schedule_installed() == 1);
    served_manifest = "{\"name\":\"probe\",\"title\":\"Probe\",\"abi\":3,\"hosts\":[\"example.test\"]}";
    app_request_t asked = { .tag = HOST_REQUEST_FIRST + 3, .method = HOST_HTTP_GET,
        .url = "https://unlisted.test/api" };
    CHECK(poller_request("probe", &asked) == HOST_ERR_ARGUMENT);
    CHECK(!run_pending_request(0) && deliveries == 0);

    strcpy(asked.url, "https://example.test/api");
    page_memory = false;
    CHECK(poller_request("probe", &asked) == HOST_ERR_NO_SPACE);
    CHECK(!run_pending_request(0) && deliveries == 0);
    page_memory = true;

    CHECK(poller_request("probe", &asked) == HOST_OK);
    CHECK(raw_sent == 0 && deliveries == 0);
    CHECK(run_pending_request(0) && raw_sent == 1 && deliveries == 1);
    CHECK(delivered_request == asked.tag && delivered_status == 200);
    CHECK(!run_pending_request(0));

    asked.tag++;
    CHECK(poller_request("probe", &asked) == HOST_OK);
    CHECK(deliveries == 1);
    CHECK(run_pending_request(HOST_FAILED_NO_NETWORK) && failures_told == 1 && deliveries == 2);
    CHECK(delivered_request == asked.tag && delivered_status == HOST_FAILED_NO_NETWORK);
    CHECK(!run_pending_request(0));
}

static void test_settings_save_during_retry_after(void)
{
    for (int dispatch_before_settings = 0; dispatch_before_settings <= 1; dispatch_before_settings++) {
        reset_poller();
        CHECK(schedule_installed() == 1);
        retry_after = 300;
        advance_to(0);
        CHECK(run_wanted_poll() && requests_sent == 1);

        advance_to(1000);
        poller_hurry("probe");
        if (dispatch_before_settings) CHECK(run_wanted_poll() && requests_sent == 1);
        /* The settings handler adjusts the interval after the host queues a refresh. */
        CHECK(poller_set_interval("probe", 0, 301) == HOST_OK);
        CHECK(poller_set_interval("probe", 0, 300) == HOST_OK);
        while (run_wanted_poll()) { }
        poller_network_is_back();
        advance_to(299999);
        CHECK(!run_wanted_poll() && requests_sent == 1);
        advance_to(301000);
        CHECK(run_wanted_poll() && requests_sent == 2);
        CHECK(!run_wanted_poll());
    }
}

static void test_retry_after_defers_startup_polls(void)
{
    reset_poller();
    installed_apps[0].poll_count = 4;
    for (int i = 0; i < 4; i++) installed_apps[0].poll_every_s[i] = 86400;
    installed_count = 2;
    strcpy(installed_apps[1].name, "other");
    installed_apps[1].id = 2;
    installed_apps[1].poll_count = 1;
    installed_apps[1].poll_every_s[0] = 3600;
    CHECK(schedule_installed() == 5);

    retry_after = 300;
    advance_to(0);
    CHECK(run_wanted_poll() && requests_sent == 1);
    advance_to(8000);
    while (run_wanted_poll()) { }
    CHECK(requests_sent == 2); /* only the other application's poll */
    advance_to(299999);
    CHECK(!run_wanted_poll());
    advance_to(300000);
    while (run_wanted_poll()) { }
    CHECK(requests_sent == 5);
}

static void test_retry_after_stops_list_round(void)
{
    for (int raw = 0; raw <= 1; raw++) {
        reset_poller();
        installed_apps[0].poll_every_s[0] = 60;
        round_items = 3;
        CHECK(schedule_installed() == 1);
        advance_to(0);
        CHECK(run_wanted_poll() && requests_sent == 1 && sent_item == 0);
        advance_to(1000);
        retry_after = 300;
        if (raw) {
            CHECK(request_queue_add(&s_queue, 1, "probe", HOST_REQUEST_FIRST,
                  HOST_HTTP_GET, "https://example.test", NULL, 0, NULL, 0, clock_ms) == REQUEST_QUEUE_ADDED);
            CHECK(run_pending_request(0) && raw_sent == 1);
        }
        CHECK(run_wanted_poll());
        CHECK(requests_sent == (raw ? 1 : 2));
        poller_hurry("probe");
        while (run_wanted_poll()) { }
        advance_to(300999);
        CHECK(!run_wanted_poll());
        advance_to(301000);
        CHECK(run_wanted_poll() && sent_item == 0);
        CHECK(run_wanted_poll() && sent_item == 1);
        CHECK(run_wanted_poll() && sent_item == 2);
        CHECK(!run_wanted_poll());
    }
}

static void test_list_round_stops_when_body_outlasts_retry_after(void)
{
    for (int raw = 0; raw <= 1; raw++) {
        reset_poller();
        installed_apps[0].poll_every_s[0] = 60;
        round_items = 3;
        CHECK(schedule_installed() == 1);
        advance_to(0);
        if (raw) {
            CHECK(run_wanted_poll() && sent_item == 0);
        }
        retry_after = 1;
        response_ms = 1500;
        if (raw) {
            CHECK(request_queue_add(&s_queue, 1, "probe", HOST_REQUEST_FIRST,
                  HOST_HTTP_GET, "https://example.test", NULL, 0, NULL, 0, clock_ms) == REQUEST_QUEUE_ADDED);
            CHECK(run_pending_request(0) && raw_sent == 1);
        }
        CHECK(run_wanted_poll() && requests_sent == 1);
        CHECK(!run_wanted_poll() && requests_sent == 1);

        response_ms = 0;
        advance_to(61499);
        CHECK(!run_wanted_poll());
        advance_to(61500);
        CHECK(run_wanted_poll() && sent_item == 0 && requests_sent == 2);
        CHECK(run_wanted_poll() && sent_item == 1);
        CHECK(run_wanted_poll() && sent_item == 2);
        CHECK(!run_wanted_poll());
    }
}

static void test_expired_retry_after_cancels_suspended_rounds(void)
{
    reset_poller();
    installed_apps[0].poll_count = 2;
    installed_apps[0].poll_every_s[0] = 60;
    installed_apps[0].poll_every_s[1] = 60;
    round_items = 3;
    round_poll = -1;
    CHECK(schedule_installed() == 2);
    advance_to(0);
    CHECK(run_wanted_poll() && sent_item == 0);
    advance_to(2000);
    CHECK(run_wanted_poll() && sent_item == 0);

    retry_after = 1;
    response_ms = 1500;
    CHECK(request_queue_add(&s_queue, 1, "probe", HOST_REQUEST_FIRST,
          HOST_HTTP_GET, "https://example.test", NULL, 0, NULL, 0, clock_ms) == REQUEST_QUEUE_ADDED);
    CHECK(run_pending_request(0));
    response_ms = 0;

    CHECK(poller_poll_now("probe", 0)); /* A newly requested round is still wanted. */
    CHECK(run_wanted_poll() && requests_sent == 2);
    CHECK(run_wanted_poll() && requests_sent == 2);
    CHECK(run_wanted_poll() && requests_sent == 3 && sent_item == 0);
    CHECK(run_wanted_poll() && sent_item == 1);
    CHECK(run_wanted_poll() && sent_item == 2);
    CHECK(!run_wanted_poll());
}

static void test_long_retry_after_survives_reload(void)
{
    reset_poller();
    installed_apps[0].poll_every_s[0] = 60;
    CHECK(schedule_installed() == 1);
    retry_after = 172800;
    advance_to(0);
    CHECK(run_wanted_poll() && requests_sent == 1);

    /* An unloaded application lost its saved cooldown and asks for a manual
     * refresh after waking. The poller still has the original server delay. */
    CHECK(poller_set_interval("probe", 0, 86400) == HOST_OK);
    poller_reload();
    advance_to(86400000);
    CHECK(poller_poll_now("probe", 0));
    CHECK(run_wanted_poll() && requests_sent == 1);
    poller_network_is_back();
    advance_to(172799999);
    CHECK(!run_wanted_poll() && requests_sent == 1);
    advance_to(172800000);
    CHECK(run_wanted_poll() && requests_sent == 2);
}

int main(void)
{
    test_request_admission();
    test_long_retry_after_survives_reload();
    test_list_round_stops_when_body_outlasts_retry_after();
    test_expired_retry_after_cancels_suspended_rounds();
    test_settings_save_during_retry_after();
    test_retry_after_defers_startup_polls();
    test_retry_after_stops_list_round();
    test_reconnect_offline_poll();
    test_reconnect_keeps_normal_pace();
    test_reconnect_transport_and_races();
    test_reconnect_due_and_cooldown();
    test_reconnect_each_and_deferred_poll();
    test_reconnect_raw_queue();
    test_local_failure_keeps_pace();
    test_each_cooldown_survives_transport_failure();
    test_abandoned_methods();
    test_incompatible_apps();
    test_interval_below_saving_floor();
    test_wake_without_address();
    REPORT();
}
