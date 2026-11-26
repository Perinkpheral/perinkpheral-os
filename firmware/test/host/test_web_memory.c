/* SPDX-License-Identifier: MIT */
#include <stdlib.h>
#include <string.h>
#include "tinytest.h"
#include "web_server.h"
#include "deadlines.h"

static int pack_allocations, received_chunks, installs;
static bool refuse_pack_allocation, area_available;
static const char *response_status;
static json_t *response_json;
static uint32_t free_bytes = 100 * 1024;
static size_t block_bytes = 64 * 1024;

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
#include "route_stubs/unrelated.h"

uint32_t esp_get_free_heap_size(void) { return free_bytes; }
uint32_t esp_get_minimum_free_heap_size(void) { return free_bytes; }
size_t heap_caps_get_largest_free_block(uint32_t caps) { (void)caps; return block_bytes; }
int64_t esp_timer_get_time(void) { return 0; }
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

/* The waiting branch runs at once here, and only where a test expects it. */
static bool waits_expected;
static int  waited;
static esp_err_t waited_handler(httpd_req_t *request) { (void)request; waited++; return ESP_OK; }
esp_err_t httpd_req_async_handler_begin(httpd_req_t *request, httpd_req_t **copy)
{ CHECK(waits_expected); *copy = request; return ESP_OK; }
esp_err_t httpd_req_async_handler_complete(httpd_req_t *request) { (void)request; return ESP_OK; }
void web_connections_complete(httpd_req_t *request, esp_err_t result) { (void)request; (void)result; }
void vTaskDelete(void *task) { (void)task; }
int xTaskCreate(void (*task)(void *), const char *name, unsigned stack, void *argument, unsigned priority, void *handle)
{ (void)name; (void)stack; (void)priority; (void)handle; CHECK(waits_expected); task(argument); return pdPASS; }
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

    /* A waiting route's session follows the poll in progress, and the poller
     * asks for its room then: on arrival, with what such a poll leaves, it
     * reaches its handler and nothing is counted. */
    /* The production route's declaration, with a handler of the test's own. */
    web_route_t catalog = { .uri = WEB_APPS_ROUTES[8].uri, .method = WEB_APPS_ROUTES[8].method,
                            .handler = waited_handler, .memory = WEB_APPS_ROUTES[8].memory,
                            .scope = WEB_APPS_ROUTES[8].scope, .pace = WEB_APPS_ROUTES[8].pace };
    CHECK(strcmp(catalog.uri, "/api/catalog") == 0 && catalog.method == HTTP_GET);
    CHECK(catalog.memory == MEMORY_FOR_A_SESSION && catalog.pace == WEB_WAITS);
    request = (httpd_req_t){ .user_ctx = (void *)&catalog };
    memory_counts_t before, after;
    memory_read(&before);
    free_bytes = 30 * 1024;
    waits_expected = true;
    CHECK(answer_route(&request) == ESP_OK && waited == 1);
    waits_expected = false;
    memory_read(&after);
    CHECK(after.refusals == before.refusals);
    /* Less than a page's worth is refused before the handler, and counted. */
    free_bytes = 20 * 1024;
    CHECK(answer_route(&request) == ESP_OK && waited == 1);
    CHECK(strcmp(response_status, "503 Service Unavailable") == 0);
    memory_read(&after);
    CHECK(after.refusals == before.refusals + 1);
    /* The same need on the server's own task is asked whole, at once. */
    free_bytes = 30 * 1024;
    catalog.pace = WEB_AT_ONCE;
    response_status = "";
    CHECK(answer_route(&request) == ESP_OK && waited == 1);
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
