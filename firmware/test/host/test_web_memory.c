/* SPDX-License-Identifier: MIT */
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include "tinytest.h"
#include "web_server.h"
#include "deadlines.h"

static int pack_allocations, received_chunks, installs;
static bool refuse_pack_allocation, area_available;
static const char *response_status;
static json_t *response_json;
static uint32_t free_bytes = 100 * 1024;
static size_t block_bytes = 64 * 1024;
static int64_t clock_ms;

#define TASK_WAIT_SECONDS 5
#define STREAMED_POLL_MS (5 * SECONDS_PER_MINUTE * MS_PER_SECOND)

static void *upload_malloc(size_t size)
{
    pack_allocations++;
    return refuse_pack_allocation ? NULL : malloc(size);
}

#define TAG apps_tag
#define handle_settings handle_app_settings
#define malloc upload_malloc
#include "../../main/web_apps.c"
#undef malloc
#undef handle_settings
#undef TAG
#undef SETTINGS_BODY_MAX
#include "../../main/web_server.c"
#define TAG poller_tag
#include "../../main/poller.c"
#undef TAG
#include "route_stubs/unrelated.h"

uint32_t esp_get_free_heap_size(void) { return free_bytes; }
uint32_t esp_get_minimum_free_heap_size(void) { return free_bytes; }
size_t heap_caps_get_largest_free_block(uint32_t caps) { (void)caps; return block_bytes; }
int64_t esp_timer_get_time(void) { return clock_ms * US_PER_MS; }
int esp_reset_reason(void) { return 1; }
bool m3_area_available(void) { return area_available; }
size_t m3_area_most_used(void) { return area_available ? 1234 : 0; }
const esp_app_desc_t *esp_app_get_description(void)
{
    static const esp_app_desc_t description = { .version = "test" };
    return &description;
}
const esp_partition_t *esp_ota_get_running_partition(void) { return NULL; }
bool ota_is_on_probation(void) { return false; }
uint32_t image_layout_partition_table(void) { return 1; }
bool net_time_is_known(void) { return false; }
int64_t net_time_now(void) { return 0; }
bool crash_note_text(char *text, size_t size) { (void)text; (void)size; return false; }
bool usb_serial_jtag_is_connected(void) { return false; }
bool storage_is_mounted(void) { return true; }
void storage_usage(size_t *used, size_t *total) { *used = 0; *total = 1; }
void settings_read(appconf_t *settings)
{
    memset(settings, 0, sizeof *settings);
    strcpy(settings->hostname, "test");
}
void wifi_get_status(wifi_status_t *status)
{
    memset(status, 0, sizeof *status);
    status->phase = WIFI_PHASE_ONLINE;
}
bool wifi_signal(int *rssi) { (void)rssi; return false; }
esp_err_t httpd_req_get_hdr_value_str(httpd_req_t *request, const char *key, char *text, size_t size)
{
    (void)request;
    if (strcmp(key, "Host") != 0) return ESP_ERR_NOT_FOUND;
    CHECK(size >= sizeof "test");
    strcpy(text, "test");
    return ESP_OK;
}
esp_err_t httpd_resp_set_status(httpd_req_t *request, const char *status)
{
    (void)request;
    response_status = status;
    return ESP_OK;
}
esp_err_t httpd_resp_sendstr(httpd_req_t *request, const char *text)
{
    (void)request; (void)text;
    CHECK(false);
    return ESP_FAIL;
}
esp_err_t web_send_json(httpd_req_t *request, const char *status, json_t *json)
{
    (void)request;
    response_status = status;
    json_free(response_json);
    response_json = json;
    return ESP_OK;
}
esp_err_t web_send_message(httpd_req_t *request, const char *status, const char *field, const char *message)
{
    json_t *json = json_new_object();
    json_add_string(json, field, message);
    return web_send_json(request, status, json);
}
int httpd_req_recv(httpd_req_t *request, char *bytes, size_t size)
{
    (void)request;
    received_chunks++;
    memset(bytes, 0, size);
    return (int)size;
}
bool events_call(void (*action)(void *), void *argument) { action(argument); return true; }
bool glyphs_install(const uint8_t *pack, size_t size)
{
    CHECK(pack != NULL && size > 0);
    installs++;
    return true;
}
bool glyphs_remove(const char *name) { (void)name; CHECK(false); return false; }
void host_redraw(bool full) { (void)full; }

static void (*deferred_task)(void *);
static void *deferred_argument;
static httpd_req_t async_request;
static int async_begins, async_completions;
static esp_err_t completion_result;
static pthread_t waiting_thread;
static pthread_mutex_t task_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t task_changed = PTHREAD_COND_INITIALIZER;
static bool poller_lock = true, fetch_turn = true, fetch_done;
static int fetch_waits, fetch_signals, poller_wakes, fetches, polls;
static bool poll_active, fetch_whole, fetch_without_memory;
static char catalog_body[64];
static size_t catalog_bytes;
static int final_chunks;

static void wait_for_task_change(void)
{
    struct timespec until;
    assert(clock_gettime(CLOCK_REALTIME, &until) == 0);
    until.tv_sec += TASK_WAIT_SECONDS;
    assert(pthread_cond_timedwait(&task_changed, &task_lock, &until) == 0);
}

int xSemaphoreTake(SemaphoreHandle_t semaphore, unsigned wait)
{
    assert(wait == portMAX_DELAY);
    assert(pthread_mutex_lock(&task_lock) == 0);
    if (semaphore == &fetch_done) {
        fetch_waits++;
        assert(pthread_cond_broadcast(&task_changed) == 0);
    }
    while (!*semaphore) wait_for_task_change();
    *semaphore = false;
    assert(pthread_mutex_unlock(&task_lock) == 0);
    return pdTRUE;
}

void xSemaphoreGive(SemaphoreHandle_t semaphore)
{
    assert(pthread_mutex_lock(&task_lock) == 0);
    assert(!*semaphore);
    *semaphore = true;
    if (semaphore == &fetch_done) fetch_signals++;
    assert(pthread_cond_broadcast(&task_changed) == 0);
    assert(pthread_mutex_unlock(&task_lock) == 0);
}

void xTaskNotifyGive(TaskHandle_t task) { assert(task != NULL); poller_wakes++; }
unsigned ulTaskNotifyTake(int clear, TickType_t timeout)
{ assert(clear == pdTRUE && timeout > 0); return 0; }
bool events_is_host_loop(void) { return false; }
void events_call_patiently(events_call_fn action, void *argument) { action(argument); }
bool wifi_is_online(void) { return true; }
bool power_is_saving(void) { return false; }
void power_hold_radio(void) { }
void power_release_radio(void) { }

static app_manifest_t installed_app;
const app_manifest_t *launcher_installed(int *count) { *count = 1; return &installed_app; }
const app_manifest_t *launcher_find(const char *name)
{ return strcmp(name, installed_app.name) == 0 ? &installed_app : NULL; }
uint32_t app_kv_poll_pace(const char *name, int poll) { (void)name; (void)poll; return 0; }
int32_t app_kv_get(const char *name, const char *key, void *value, int32_t size)
{ (void)name; (void)key; (void)value; (void)size; return HOST_ERR_NOT_FOUND; }
void deadlines_cancel(deadline_kind_t kind, uint32_t owner, int number)
{ (void)kind; (void)owner; (void)number; }
bool deadlines_has(deadline_kind_t kind, uint32_t owner, int number)
{ (void)kind; (void)owner; (void)number; return false; }
void deadlines_keep_only(deadline_kind_t kind, const uint32_t *owners, int count)
{ (void)kind; (void)owners; (void)count; }

bool web_query_value(httpd_req_t *request, const char *key, char *value, size_t size)
{
    assert(request == &async_request && strcmp(key, "source") == 0 && size >= 2);
    strcpy(value, "0");
    return true;
}
void settings_load_sources(char *text, size_t size)
{ assert(size >= sizeof CATALOG_OFFICIAL_SOURCE); strcpy(text, CATALOG_OFFICIAL_SOURCE); }
esp_err_t httpd_resp_set_type(httpd_req_t *request, const char *type)
{
    assert(request == &async_request && strcmp(type, "application/json") == 0);
    return ESP_OK;
}
esp_err_t httpd_resp_send_chunk(httpd_req_t *request, const char *piece, ssize_t length)
{
    assert(request == &async_request && length >= 0);
    if (length == 0) {
        assert(piece == NULL);
        final_chunks++;
    } else {
        assert(catalog_bytes + (size_t)length < sizeof catalog_body);
        memcpy(catalog_body + catalog_bytes, piece, (size_t)length);
        catalog_bytes += (size_t)length;
        catalog_body[catalog_bytes] = '\0';
    }
    return ESP_OK;
}

esp_err_t httpd_req_async_handler_begin(httpd_req_t *request, httpd_req_t **copy)
{
    assert(deferred_task == NULL);
    async_begins++;
    async_request = *request;
    *copy = &async_request;
    return ESP_OK;
}
esp_err_t httpd_req_async_handler_complete(httpd_req_t *request)
{ assert(request == &async_request); async_completions++; return ESP_OK; }
void web_connections_complete(httpd_req_t *request, esp_err_t result)
{ completion_result = result; httpd_req_async_handler_complete(request); }
void vTaskDelete(void *task) { (void)task; }
int xTaskCreate(void (*task)(void *), const char *name, unsigned stack, void *argument,
                unsigned priority, TaskHandle_t *handle)
{
    assert(strcmp(name, "web_waits") == 0 && stack == STACK_WEB_WAITS && priority == PRIORITY_SERVER);
    assert(handle == NULL && deferred_task == NULL);
    deferred_task = task;
    deferred_argument = argument;
    return pdPASS;
}

static void *run_waiting_task(void *argument)
{ (void)argument; deferred_task(deferred_argument); return NULL; }

int poller_net_poll(const char *name, int poll, int item, int *method, bool *network_failed)
{
    (void)item;
    CHECK_STR(name, "probe");
    CHECK(poll == 0);
    poll_active = true;
    polls++;
    free_bytes = 30 * 1024;
    const web_route_t catalog = WEB_APPS_ROUTES[8];
    CHECK(strcmp(catalog.uri, "/api/catalog") == 0 && catalog.handler == handle_catalog);
    CHECK(catalog.memory == MEMORY_FOR_A_SESSION && catalog.pace == WEB_WAITS);
    httpd_req_t request = { .uri = catalog.uri, .user_ctx = (void *)&catalog };
    memory_counts_t before, after;
    memory_read(&before);
    CHECK(answer_route(&request) == ESP_OK);
    CHECK(deferred_task != NULL && atomic_load(&s_is_waiting));
    CHECK(s_fetch_job == NULL && async_begins == polls && async_completions == polls - 1);
    CHECK(answer_route(&request) == ESP_OK);
    CHECK_STR(json_get(response_json, "error")->text, "Device busy.");
    CHECK(async_begins == polls);
    const web_route_t status = ROUTES[1];
    request.user_ctx = (void *)&status;
    CHECK(answer_route(&request) == ESP_OK);
    CHECK_STR(response_status, "200 OK");
    memory_read(&after);
    CHECK(after.refusals == before.refusals);

    int wakes_before = poller_wakes;
    CHECK(pthread_create(&waiting_thread, NULL, run_waiting_task, NULL) == 0);
    assert(pthread_mutex_lock(&task_lock) == 0);
    while (fetch_waits < polls) wait_for_task_change();
    CHECK(s_fetch_job != NULL && !fetch_turn && !fetch_done);
    CHECK(poller_wakes == wakes_before + 1 && fetch_signals == polls - 1);
    CHECK(async_completions == polls - 1 && catalog_bytes == 0);
    assert(pthread_mutex_unlock(&task_lock) == 0);
    /* A streamed poll can keep the fetch queued beyond its memory-wait limit. */
    clock_ms += STREAMED_POLL_MS;
    free_bytes = fetch_without_memory ? 30 * 1024 : 100 * 1024;
    poll_active = false;
    *method = HOST_HTTP_GET;
    *network_failed = false;
    return 0;
}

void poller_net_poll_not_made(const char *name, int poll, int why)
{ (void)name; (void)poll; (void)why; assert(false); }
bool poller_net_fetch(const char *url, size_t max_bytes, poller_net_take_fn take, void *context, int *status)
{
    assert(!poll_active && !fetch_without_memory);
    assert(strcmp(url, CATALOG_OFFICIAL_SOURCE "/" CATALOG_INDEX_FILE) == 0);
    assert(max_bytes == CATALOG_INDEX_MAX_BYTES);
    fetches++;
    *status = 200;
    static const uint8_t first[] = "{", second[] = "\"apps\":[]}";
    return take(context, first, sizeof first - 1) &&
           take(context, second, sizeof second - 1) && fetch_whole;
}
int heap_caps_register_failed_alloc_callback(void (*callback)(size_t, uint32_t, const char *))
{ (void)callback; return 0; }
bool deadlines_set_in(deadline_kind_t kind, uint32_t owner, int number, int64_t in_ms)
{ (void)kind; (void)owner; (void)number; (void)in_ms; return true; }
void host_stop_soon(host_stop_t why) { (void)why; CHECK(false); }

static void check_refusal(void)
{
    CHECK(strcmp(response_status, "503 Service Unavailable") == 0);
    const json_t *error = json_get(response_json, "error");
    CHECK(json_is_string(error) && strcmp(error->text, "Not enough memory.") == 0);
    CHECK(received_chunks == 0 && installs == 0);
}

static void test_waiting_catalog(void)
{
    s_lock = &poller_lock;
    s_fetch_turn = &fetch_turn;
    s_fetch_done = &fetch_done;
    s_task = &installed_app;
    strcpy(installed_app.name, "probe");
    installed_app.id = 1;
    installed_app.abi = HOST_API_VERSION;
    installed_app.poll_count = 1;
    installed_app.poll_every_s[0] = 3600;
    poll_schedule_init(&s_schedule);
    CHECK(schedule_installed() == 1);
    int expected_fetches = 0;
    const struct { bool whole, no_memory; } cases[] = {
        { true, false }, { false, false }, { true, true }, { true, false },
    };
    const int case_count = sizeof cases / sizeof cases[0];
    for (int attempt = 0; attempt < case_count; attempt++) {
        fetch_whole = cases[attempt].whole;
        fetch_without_memory = cases[attempt].no_memory;
        final_chunks = 0;
        catalog_bytes = 0;
        free_bytes = 100 * 1024;
        poller_poll_is_due(1, 0);
        CHECK(run_wanted_poll());
        CHECK(s_fetch_job != NULL && async_completions == attempt);
        CHECK(run_fetch_job());
        if (fetch_without_memory) {
            CHECK(s_fetch_job != NULL && async_completions == attempt);
            clock_ms += FETCH_MEMORY_WAIT_MS;
            CHECK(run_fetch_job());
        } else {
            expected_fetches++;
        }
        CHECK(pthread_join(waiting_thread, NULL) == 0);
        CHECK(!atomic_load(&s_is_waiting) && s_fetch_job == NULL);
        CHECK(fetch_turn && !fetch_done && fetch_signals == attempt + 1);
        CHECK(async_completions == attempt + 1 && fetches == expected_fetches);
        CHECK(completion_result == (fetch_whole ? ESP_OK : ESP_FAIL));
        if (fetch_without_memory) {
            CHECK_STR(response_status, "503 Service Unavailable");
            CHECK_STR(json_get(response_json, "error")->text, "Not enough memory.");
            CHECK(catalog_bytes == 0 && final_chunks == 0);
        } else {
            CHECK_STR(catalog_body, "{\"apps\":[]}");
            CHECK(final_chunks == (fetch_whole ? 1 : 0));
        }
        CHECK(!run_fetch_job() && fetch_signals == attempt + 1);
        deferred_task = NULL;
    }
}

int main(void)
{
    /* Copy the production declarations; keeping the indices explicit also checks
     * which URI and method actually receive the admission and status handlers. */
    const web_route_t upload = WEB_APPS_ROUTES[4];
    CHECK(strcmp(upload.uri, "/api/glyphs") == 0 && upload.method == HTTP_POST);
    httpd_req_t request = { .content_len = GLYPHS_PACK_MAX_BYTES, .user_ctx = (void *)&upload };
    free_bytes = 40 * 1024;
    CHECK(answer_route(&request) == ESP_OK);
    check_refusal();
    CHECK(pack_allocations == 0);
    free_bytes = 100 * 1024;
    block_bytes = 32 * 1024;
    CHECK(answer_route(&request) == ESP_OK);
    check_refusal();
    CHECK(pack_allocations == 0);
    block_bytes = 64 * 1024;
    refuse_pack_allocation = true;
    CHECK(answer_route(&request) == ESP_OK);
    check_refusal();
    CHECK(pack_allocations == 1);
    refuse_pack_allocation = false;
    CHECK(answer_route(&request) == ESP_OK);
    CHECK(strcmp(response_status, "200 OK") == 0);
    CHECK(pack_allocations == 2 && received_chunks == 1 && installs == 1);

    test_waiting_catalog();

    web_route_t catalog = WEB_APPS_ROUTES[8];
    request = (httpd_req_t){ .user_ctx = (void *)&catalog };
    memory_counts_t before, after;
    memory_read(&before);
    /* Less than a page's worth is refused before the handler, and counted. */
    free_bytes = 20 * 1024;
    CHECK(answer_route(&request) == ESP_OK && deferred_task == NULL);
    CHECK(strcmp(response_status, "503 Service Unavailable") == 0);
    memory_read(&after);
    CHECK(after.refusals == before.refusals + 1);
    /* The same need on the server's own task is asked whole, at once. */
    free_bytes = 30 * 1024;
    catalog.pace = WEB_AT_ONCE;
    response_status = "";
    CHECK(answer_route(&request) == ESP_OK && deferred_task == NULL);
    CHECK(strcmp(response_status, "503 Service Unavailable") == 0);
    memory_read(&after);
    CHECK(after.refusals == before.refusals + 2);
    free_bytes = 100 * 1024;

    const web_route_t status = ROUTES[1];
    CHECK(strcmp(status.uri, "/api/status") == 0 && status.method == HTTP_GET);
    request.user_ctx = (void *)&status;
    free_bytes = block_bytes = 0;
    for (int available = 0; available <= 1; available++) {
        area_available = available;
        CHECK(answer_route(&request) == ESP_OK);
        CHECK(strcmp(response_status, "200 OK") == 0);
        const json_t *field = json_get(response_json, "app_area_available");
        CHECK(json_is_bool(field) && (field->type == JSON_TRUE) == area_available);
        CHECK(json_get(response_json, "app_area_used")->number == (available ? 1234 : 0));
    }
    json_free(response_json);
    REPORT();
}
