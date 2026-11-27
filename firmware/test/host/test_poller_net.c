/* SPDX-License-Identifier: MIT */
/* The real HTTP fetcher with in-memory reads and no network sockets. */
#include "tinytest.h"
#include <stdlib.h>
#include "freertos/task.h"
static void *request_calloc(size_t count, size_t size);
static void *request_malloc(size_t size);
#define calloc request_calloc
#define malloc request_malloc
#define time poller_test_time
#include "../../main/poller_net.c"
#undef time
#undef calloc
#undef malloc

static int json_allocations, fail_json_at;
static bool json_only_authorization, in_host_loop;
static bool use_setting_defaults;
static const char *fail_default_key;
static int default_lookups;
/* ESP-IDF allocators need not set errno when they return NULL. */
static bool refuse_json_allocation(void)
{
    return (!json_only_authorization || in_host_loop) && ++json_allocations == fail_json_at;
}
static void *json_test_calloc(size_t count, size_t size)
{
    return refuse_json_allocation() ? NULL : calloc(count, size);
}
static void *json_test_malloc(size_t size)
{
    return refuse_json_allocation() ? NULL : malloc(size);
}
#define calloc json_test_calloc
#define malloc json_test_malloc
#include "../../components/appstore/json_tree.c"
#undef calloc
#undef malloc

static bool fail_request_allocation, fail_list_allocation, fail_client_init, fail_manifest;
static esp_err_t header_error;
static void *request_calloc(size_t count, size_t size)
{
    return fail_request_allocation ? NULL : calloc(count, size);
}
static void *request_malloc(size_t size)
{
    return fail_list_allocation && size == HOST_LIST_BYTES ? NULL : malloc(size);
}

static time_t wall_time = 1795780800;
static int clock_reads;
time_t poller_test_time(time_t *result)
{
    clock_reads++;
    if (result != NULL) *result = wall_time;
    return wall_time;
}

static int response_status, read_failure, read_calls, cleanups, consumer_calls;
static size_t body_bytes, received;
static int64_t clock_ms;
static bool consumer_accepts, fail_open, expire_read;
static bool parsed_ca, parsed_extra;
static int certificate_parse_calls;
static bool mutex_taken, online = true, complete_body = true;
static int socket_error, tls_error_code, certificate_flags, header_failure;
static esp_err_t open_error, tls_failure;
static bool failed_before_first_with_type;
static int delivered_status;
static bool delivered_only_on_screen;
static const char *manifest_json;
static esp_http_client_config_t active_client;
static char *retry_header;
static int cooldown_seconds, cooldown_calls;
static int64_t content_length_override = -2;
static const char *content_type = "application/json";
static const char MOCK_BODY[] = "{\"value\":42}";
static bool mock_success, mock_picked;
static int delivered_replies;
static bool refuse_rest;
static int delivered_pieces, last_flags, header_deliveries;
static bool delivered_closing, refuse_headers;

SemaphoreHandle_t xSemaphoreCreateMutex(void) { return &mutex_taken; }
int xSemaphoreTake(SemaphoreHandle_t lock, unsigned wait) { (void)wait; CHECK(!*lock); *lock = true; return pdTRUE; }
void xSemaphoreGive(SemaphoreHandle_t lock) { CHECK(*lock); *lock = false; }
int64_t esp_timer_get_time(void) { return clock_ms * US_PER_MS; }
esp_err_t esp_timer_create(const esp_timer_create_args_t *args, esp_timer_handle_t *timer) { (void)args; *timer = &clock_ms; return ESP_OK; }
esp_err_t esp_timer_stop(esp_timer_handle_t timer) { (void)timer; return ESP_OK; }
esp_err_t esp_timer_start_once(esp_timer_handle_t timer, uint64_t delay) { (void)timer; (void)delay; return ESP_OK; }
int test_shutdown(int socket, int how) { (void)socket; (void)how; CHECK(false); return 0; }
const char *esp_err_to_name(esp_err_t error) { (void)error; return "test error"; }
esp_err_t esp_crt_bundle_attach(void *config) { (void)config; return ESP_OK; }
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *config) {
    if (fail_client_init) return NULL;
    CHECK_STR(config->url, "https://example.test/file");
    active_client = *config;
    return &response_status;
}
esp_err_t esp_http_client_open(esp_http_client_handle_t client, int length) { (void)client; CHECK(length == 0); return fail_open ? open_error : ESP_OK; }
int esp_http_client_get_socket(esp_http_client_handle_t client) { (void)client; return -1; }
static void reply_header(char *name, char *value)
{
    esp_http_client_event_t event = { .event_id = HTTP_EVENT_ON_HEADER,
        .user_data = active_client.user_data, .header_key = name, .header_value = value };
    CHECK(active_client.event_handler(&event) == ESP_OK);
}
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t client)
{
    (void)client;
    if (header_failure != 0) return header_failure;
    if (active_client.event_handler) reply_header("Content-Type", (char *)content_type);
    if (retry_header != NULL) reply_header("Retry-After", retry_header);
    if (mock_success) {
        reply_header("ETag", "\"revision-1\"");
        reply_header("Set-Cookie", "sid=probe");
    }
    return (int64_t)body_bytes;
}
int esp_http_client_get_status_code(esp_http_client_handle_t client) { (void)client; return response_status; }
int esp_http_client_read(esp_http_client_handle_t client, char *bytes, int capacity) {
    (void)client; read_calls++;
    if (body_bytes > 0) {
        size_t count = body_bytes < (size_t)capacity ? body_bytes : (size_t)capacity;
        if (mock_success) {
            memcpy(bytes, MOCK_BODY + sizeof MOCK_BODY - 1 - body_bytes, count);
        } else {
            memset(bytes, 'x', count);
        }
        body_bytes -= count;
        return (int)count;
    }
    if (expire_read) clock_ms += FIRMWARE_WITHIN_MS;
    return read_failure;
}
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t client) { (void)client; cleanups++; CHECK(s_cut.socket == -1); return ESP_OK; }
esp_http_client_transport_t esp_http_client_get_transport_type(esp_http_client_handle_t client)
{
    (void)client;
    return HTTP_TRANSPORT_OVER_TCP;
}
esp_err_t esp_http_client_set_redirection(esp_http_client_handle_t client) { (void)client; return ESP_FAIL; }
esp_err_t esp_http_client_clear_response_buffer(esp_http_client_handle_t client) { (void)client; return ESP_OK; }
esp_err_t esp_http_client_close(esp_http_client_handle_t client) { (void)client; return ESP_OK; }
esp_err_t esp_http_client_get_url(esp_http_client_handle_t client, char *url, int capacity) { (void)client; (void)url; (void)capacity; return ESP_FAIL; }
esp_err_t esp_http_client_set_header(esp_http_client_handle_t client, const char *name, const char *value) { (void)client; (void)name; (void)value; return header_error; }
esp_err_t esp_http_client_delete_header(esp_http_client_handle_t client, const char *name) { (void)client; (void)name; return ESP_OK; }
int esp_http_client_write(esp_http_client_handle_t client, const char *bytes, int length) { (void)client; (void)bytes; return length; }
esp_err_t esp_http_client_set_timeout_ms(esp_http_client_handle_t client, int timeout) { (void)client; (void)timeout; return ESP_OK; }
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t client) { (void)client; return complete_body; }
bool esp_http_client_is_chunked_response(esp_http_client_handle_t client) { (void)client; return false; }
int64_t esp_http_client_get_content_length(esp_http_client_handle_t client) { (void)client; return content_length_override == -2 ? (int64_t)body_bytes : content_length_override; }
int esp_http_client_get_and_clear_last_tls_error(esp_http_client_handle_t client, int *error, int *flags) { (void)client; *error = tls_error_code; *flags = certificate_flags; return tls_failure; }
int esp_http_client_get_errno(esp_http_client_handle_t client) { (void)client; return socket_error; }
void mbedtls_x509_crt_init(mbedtls_x509_crt *chain) { memset(chain, 0, sizeof *chain); }
void mbedtls_x509_crt_free(mbedtls_x509_crt *chain) { (void)chain; }
int mbedtls_x509_crt_parse_der(mbedtls_x509_crt *chain, const unsigned char *bytes, size_t length) { certificate_parse_calls++; chain->ca_istrue = parsed_ca; chain->next = parsed_extra ? chain : NULL; (void)bytes; (void)length; return 0; }
void mbedtls_ssl_conf_ca_chain(mbedtls_ssl_config *config, mbedtls_x509_crt *chain, void *revocations) { (void)config; (void)chain; (void)revocations; }
int32_t app_kv_get_or_default(const char *name, const char *key, void *value, int32_t capacity)
{
    (void)name;
    if (!use_setting_defaults) return HOST_ERR_NOT_FOUND;
    default_lookups++;
    bool fail_default = fail_default_key != NULL && strcmp(key, fail_default_key) == 0;
    if (fail_default) fail_json_at = json_allocations + 1;
    int length = app_manifest_setting_default(manifest_json, key, value, (size_t)capacity);
    if (fail_default) fail_json_at = 0;
    return length;
}
char *app_store_read_manifest(const char *root, const char *name) { (void)root; (void)name; if (fail_manifest) { errno = ENOMEM; return NULL; } return manifest_json == NULL ? NULL : strdup(manifest_json); }
long app_store_file_size(const char *root, const char *name, const char *file) { (void)root; (void)name; (void)file; return -1; }
long app_store_read_file(const char *root, const char *name, const char *file, long offset, void *bytes, size_t capacity) { (void)root; (void)name; (void)file; (void)offset; (void)bytes; (void)capacity; return -1; }
void events_call_patiently(events_call_fn run, void *argument) { in_host_loop = true; run(argument); in_host_loop = false; }
bool wifi_is_online(void) { return online; }

static bool take(void *context, const uint8_t *piece, size_t length)
{
    CHECK(context == &received);
    CHECK(piece[0] == 'x');
    consumer_calls++;
    received += length;
    return consumer_accepts;
}

static void reset(void)
{
    fail_request_allocation = fail_list_allocation = fail_client_init = fail_manifest = false;
    json_allocations = fail_json_at = 0;
    json_only_authorization = in_host_loop = false;
    use_setting_defaults = false;
    fail_default_key = NULL;
    default_lookups = 0;
    header_error = ESP_OK;
    content_length_override = -2;
    content_type = "application/json";
    online = complete_body = true;
    socket_error = tls_error_code = certificate_flags = header_failure = 0;
    open_error = ESP_FAIL;
    tls_failure = ESP_OK;
    delivered_status = delivered_replies = 0;
    refuse_rest = false;
    delivered_pieces = last_flags = header_deliveries = 0;
    delivered_closing = refuse_headers = false;
    mock_success = false;
    manifest_json = NULL;
    retry_header = NULL;
    cooldown_seconds = cooldown_calls = 0;
    response_status = 200;
    body_bytes = HOST_DATA_CHUNK_MAX + 17;
    received = 0;
    read_failure = read_calls = cleanups = consumer_calls = 0;
    clock_ms = 0;
    consumer_accepts = true;
    fail_open = expire_read = false;
}

static bool fetch(size_t maximum, int *status)
{
    bool result = poller_net_fetch("https://example.test/file", maximum, take, &received, status);
    CHECK(cleanups == 1 && !mutex_taken);
    return result;
}

static app_delivery_kind_t delivered_kind;
static int32_t delivered_length;
static bool check_delivery(const char *name, const app_delivery_t *answer);
static bool deliver(const char *name, const app_delivery_t *answer)
{
    if (answer->headers_only) {
        CHECK(read_calls == 0 && answer->flags == HOST_DATA_FIRST && !answer->is_closing);
        CHECK(answer->length == 0 && answer->data == NULL && answer->response != NULL);
        CHECK(answer_format(0, answer->headers, answer->response) == HOST_FORMAT_JSON);
        header_deliveries++;
        return !refuse_headers;
    }
    delivered_pieces++;
    delivered_closing = answer->is_closing;
    CHECK(!answer->is_closing || answer->flags == HOST_DATA_FAILED);
    last_flags = answer->flags;
    check_delivery(name, answer);
    return !refuse_rest;
}

static bool check_delivery(const char *name, const app_delivery_t *answer)
{
    delivered_kind = answer->kind;
    delivered_length = answer->response != NULL ? answer->response->length : -1;
    delivered_status = answer->http_status;
    failed_before_first_with_type =
        (answer->flags & HOST_DATA_FAILED) &&
        answer_format(0, answer->headers, answer->response) == HOST_FORMAT_JSON;
    delivered_only_on_screen = answer->only_on_screen;
    if (!mock_success) return true;

    delivered_replies++;
    CHECK_STR(name, "probe");
    CHECK(answer->request == 0 && answer->http_status == 200);
    CHECK(answer->response != NULL);
    if (answer->response != NULL) {
        CHECK_STR(answer->response->etag, "\"revision-1\"");
        CHECK(answer->response->length == sizeof MOCK_BODY - 1);
    }
    char cookie[16] = {0};
    CHECK(answer_headers_get(answer->headers, "Set-Cookie", 10, 0, cookie, sizeof cookie) == 9);
    CHECK_STR(cookie, "sid=probe");
    if (mock_picked) {
        CHECK(answer->kind == APP_DELIVERY_VALUES && answer->picked != NULL);
        if (answer->picked != NULL) {
            const char *picked = json_pick_value(answer->picked, 0);
            CHECK(picked != NULL && strcmp(picked, "42") == 0);
        }
    } else {
        CHECK(answer->kind == APP_DELIVERY_PIECE);
        CHECK(answer->flags == (HOST_DATA_FIRST | HOST_DATA_LAST));
        CHECK(answer->length == sizeof MOCK_BODY - 1 && answer->data != NULL &&
              memcmp(answer->data, MOCK_BODY, sizeof MOCK_BODY - 1) == 0);
    }
    return true;
}

static void record_retry_after(int seconds)
{
    CHECK(read_calls == 0);
    cooldown_seconds = seconds;
    cooldown_calls++;
}

static bool poll_failure(bool picked)
{
    manifest_json = picked
        ? "{\"polls\":[{\"url\":\"https://example.test/file\",\"every\":3600,\"pick\":[\"value\"]}]}"
        : "{\"polls\":[{\"url\":\"https://example.test/file\",\"every\":3600}]}";
    bool network_failed = false;
    int method = -1;
    CHECK(poller_net_poll("probe", 0, 0, &method, &network_failed) == 0);
    CHECK(method == HOST_HTTP_GET);
    return network_failed;
}

static void test_poll_memory_failures(void)
{
    reset();
    fail_request_allocation = true;
    bool network_failed = false;
    int method = -1;
    CHECK(poller_net_poll("probe", 0, 0, &method, &network_failed) == 0);
    CHECK(delivered_status == HOST_FAILED_NO_MEMORY && delivered_only_on_screen);
    CHECK(delivered_kind == APP_DELIVERY_POLL_FAILED && !network_failed && method == -1);
    delivered_status = 0;
    poller_net_poll_not_made("probe", 0, HOST_FAILED_NO_MEMORY);
    CHECK(delivered_status == HOST_FAILED_NO_MEMORY && delivered_only_on_screen);
    poller_ask_t ask = { .number = INT32_MAX, .url = "https://example.test/file" };
    delivered_status = 0;
    poller_net_request("probe", &ask);
    CHECK(delivered_status == HOST_FAILED_NO_MEMORY && !delivered_only_on_screen);
    reset();
    fail_client_init = true;
    CHECK(!poll_failure(true));
    CHECK(delivered_status == HOST_FAILED_NO_MEMORY && delivered_kind == APP_DELIVERY_VALUES);
    reset();
    fail_manifest = true;
    poller_net_poll("probe", 0, 0, &method, &network_failed);
    CHECK(delivered_status == HOST_FAILED_NO_MEMORY && delivered_only_on_screen);
    reset();
    fail_manifest = true;
    poller_net_request("probe", &ask);
    CHECK(delivered_status == HOST_FAILED_NO_MEMORY && !delivered_only_on_screen);
    for (int authorization = 0; authorization <= 1; authorization++) {
        reset();
        manifest_json = "{\"hosts\":[\"example.test\"],\"polls\":[{\"url\":\"https://example.test/file\",\"every\":60}]}";
        json_only_authorization = authorization != 0;
        request_t request = { .poll = { .url = "https://example.test/file" } };
        preparation_t preparation = { .request = &request, .only_listed_hosts = true };
        request.app_name = "probe";
        if (authorization) {
            events_call_patiently(prepare_in_host_loop, &preparation);
            CHECK(preparation.failure == 0);
        } else {
            CHECK(load_poll(&request, "probe", 0));
        }
        int allocations = json_allocations;
        CHECK(allocations > 0);
        for (int allocation = 1; allocation <= allocations; allocation++) {
            json_allocations = 0;
            fail_json_at = allocation;
            delivered_status = 0;
            errno = 0;
            if (authorization) {
                poller_net_request("probe", &ask);
            } else {
                poller_net_poll("probe", 0, 0, &method, &network_failed);
            }
            CHECK(delivered_status == HOST_FAILED_NO_MEMORY);
            CHECK(delivered_only_on_screen == !authorization);
        }
    }
    for (int failed = 0; failed < 2; failed++) {
        reset();
        manifest_json = "{\"hosts\":[\"{{first}}\",\"{{second}}\"],\"settings\":["
            "{\"key\":\"first\",\"title\":\"First\",\"type\":\"url\",\"default\":\"https://example.test\"},"
            "{\"key\":\"second\",\"title\":\"Second\",\"type\":\"url\",\"default\":\"https://other.test\"}]}";
        use_setting_defaults = true;
        fail_default_key = failed == 0 ? "first" : "second";
        poller_ask_t request = { .number = INT32_MAX,
            .url = failed == 0 ? "https://example.test/file" : "https://other.test/file" };
        poller_net_request("probe", &request);
        CHECK(default_lookups == 2);
        CHECK(delivered_status == HOST_FAILED_NO_MEMORY && !delivered_only_on_screen);
        CHECK(cleanups == 0);
        default_lookups = 0;
        delivered_status = 0;
        fail_default_key = NULL;
        request.url = "https://example.test/file";
        poller_net_request("probe", &request);
        CHECK(default_lookups >= 1 && delivered_status == 200 && cleanups == 1);
    }
    reset();
    header_error = ESP_ERR_NO_MEM;
    manifest_json = "{\"polls\":[{\"url\":\"https://example.test/file\",\"every\":60,\"headers\":{\"X-Test\":\"value\"}}]}";
    poller_net_poll("probe", 0, 0, &method, &network_failed);
    CHECK(delivered_status == HOST_FAILED_NO_MEMORY && delivered_only_on_screen && cleanups == 1);
    reset();
    fail_list_allocation = true;
    manifest_json = "{\"polls\":[{\"url\":\"https://example.test/file\",\"every\":60,\"pick\":[\"value[*]\"]}]}";
    poller_net_poll("probe", 0, 0, &method, &network_failed);
    CHECK(delivered_status == HOST_FAILED_NO_MEMORY && delivered_kind == APP_DELIVERY_VALUES);

    const int allocation_errors[] = {
        -0x7F00, /* MBEDTLS_ERR_SSL_ALLOC_FAILED */
        -0x2880, /* MBEDTLS_ERR_X509_ALLOC_FAILED */
        -0x3F80, /* MBEDTLS_ERR_PK_ALLOC_FAILED */
        -0x1180, /* MBEDTLS_ERR_PEM_ALLOC_FAILED */
        -0x6180, /* MBEDTLS_ERR_CIPHER_ALLOC_FAILED */
        -0x5180, /* MBEDTLS_ERR_MD_ALLOC_FAILED */
        -0x4D80, /* MBEDTLS_ERR_ECP_ALLOC_FAILED */
        -0x3400, /* MBEDTLS_ERR_DHM_ALLOC_FAILED */
        -0x0010, /* MBEDTLS_ERR_MPI_ALLOC_FAILED */
        -0x006A, /* MBEDTLS_ERR_ASN1_ALLOC_FAILED */
        -0x4310, /* RSA private-key operation with an MPI allocation failure. */
        -0x236A, /* X509 parsing with an ASN1 allocation failure. */
        -0x7F10, /* SSL allocation failure with an MPI error. */
    };
    for (int picked = 0; picked <= 1; picked++) {
        /* ESP-TLS reports the magnitude; also accept signed library errors. */
        for (int sign = -1; sign <= 1; sign += 2) {
            for (size_t i = 0; i < sizeof allocation_errors / sizeof allocation_errors[0]; i++) {
                reset(); fail_open = true; tls_error_code = sign * allocation_errors[i];
                CHECK(!poll_failure(picked)); CHECK(delivered_status == HOST_FAILED_NO_MEMORY);
                CHECK(cleanups == 1 && !mutex_taken);
            }
        }
        reset(); fail_open = true; open_error = ESP_ERR_NO_MEM;
        CHECK(!poll_failure(picked)); CHECK(delivered_status == HOST_FAILED_NO_MEMORY);
        reset(); fail_open = true; tls_failure = ESP_ERR_NO_MEM;
        CHECK(!poll_failure(picked)); CHECK(delivered_status == HOST_FAILED_NO_MEMORY);
        const int memory_errors[] = { ENOMEM, ENOBUFS };
        for (size_t i = 0; i < sizeof memory_errors / sizeof memory_errors[0]; i++) {
            reset(); fail_open = true; socket_error = memory_errors[i];
            CHECK(!poll_failure(picked)); CHECK(delivered_status == HOST_FAILED_NO_MEMORY);
        }
    }
}

static void test_poll_transport_failures(void)
{
    for (int picked = 0; picked <= 1; picked++) {
        reset(); fail_open = true; online = false;
        CHECK(poll_failure(picked)); CHECK(delivered_status == HOST_FAILED_NO_NETWORK);
        reset(); fail_open = true; socket_error = ECONNREFUSED;
        CHECK(poll_failure(picked)); CHECK(delivered_status == HOST_FAILED_NO_ANSWER);
        /* DNS and a broken TLS handshake both fail open without a socket errno. */
        reset(); fail_open = true;
        CHECK(poll_failure(picked)); CHECK(delivered_status == HOST_FAILED_NO_ANSWER);
        reset(); fail_open = true; tls_error_code = -0x0050;
        CHECK(poll_failure(picked)); CHECK(delivered_status == HOST_FAILED_NO_ANSWER);
        reset(); header_failure = -ESP_ERR_HTTP_EAGAIN;
        CHECK(poll_failure(picked)); CHECK(delivered_status == HOST_FAILED_TIMEOUT);
        reset(); body_bytes = 0; read_failure = -1;
        CHECK(poll_failure(picked)); CHECK(delivered_status == HOST_FAILED_NO_ANSWER);
        CHECK(!delivered_closing);
        reset(); body_bytes = 0; complete_body = false;
        CHECK(poll_failure(picked)); CHECK(delivered_status == HOST_FAILED_NO_ANSWER);
        if (!picked)
            CHECK(failed_before_first_with_type);
        reset(); body_bytes = 0; read_failure = -1; expire_read = true;
        CHECK(poll_failure(picked)); CHECK(delivered_status == HOST_FAILED_TIMEOUT);
        reset(); fail_open = true; tls_error_code = -X509_VERIFY_FAILED;
        CHECK(!poll_failure(picked)); CHECK(delivered_status == HOST_FAILED_NOT_TRUSTED);
        reset(); fail_open = true; certificate_flags = 1;
        CHECK(!poll_failure(picked)); CHECK(delivered_status == HOST_FAILED_NOT_TRUSTED);
        const int refusals[] = { 401, 404, 429, 503 };
        for (size_t i = 0; i < sizeof refusals / sizeof refusals[0]; i++) {
            reset(); response_status = refusals[i];
            CHECK(!poll_failure(picked)); CHECK(delivered_status == refusals[i]);
        }
        reset();
        CHECK(!poll_failure(picked));
        CHECK(delivered_status == (picked ? HOST_FAILED_NO_ANSWER : 200));
    }
    reset(); body_bytes = HOST_ANSWER_PIECES_MAX + HOST_DATA_CHUNK_MAX; read_failure = -1;
    CHECK(!poll_failure(false)); CHECK(delivered_status == HOST_FAILED_TOO_LARGE);
    reset();
    manifest_json = "{\"polls\":[{\"url\":\"https://example.test/file\",\"every\":3600}]}";
    poller_net_poll_not_made("probe", 0, HOST_FAILED_NO_NETWORK);
    CHECK(delivered_only_on_screen && delivered_status == HOST_FAILED_NO_NETWORK);
}

static void test_successful_poll_delivery(void)
{
    for (int picked = 0; picked <= 1; picked++) {
        reset();
        mock_success = true;
        mock_picked = picked;
        body_bytes = sizeof MOCK_BODY - 1;
        CHECK(!poll_failure(picked));
        CHECK(delivered_replies == 1 && delivered_status == 200);
    }
}

/* Nobody takes the rest of a streamed answer: it is not read further, and the delivery's own
 * bookkeeping gets its end. */
static void test_unwanted_rest_is_not_read(void)
{
    reset();
    body_bytes = 64 * HOST_DATA_CHUNK_MAX;
    refuse_rest = true;
    CHECK(!poll_failure(false));
    CHECK(read_calls == 2); /* the first piece, and the one read to tell whether it is the last */
    CHECK(delivered_pieces == 2 && last_flags == HOST_DATA_FAILED);
    CHECK(delivered_status == HOST_FAILED_NO_ANSWER && delivered_closing);

    reset();
    body_bytes = 8 * HOST_DATA_CHUNK_MAX;
    CHECK(!poll_failure(false));
    CHECK(delivered_pieces == 8 && last_flags == HOST_DATA_LAST && delivered_status == 200);
}

static void test_headers_refuse_slow_body(void)
{
    reset();
    response_status = 403;
    body_bytes = 1;
    complete_body = false;
    read_failure = -1;
    expire_read = true;
    refuse_headers = true;
    CHECK(!poll_failure(false));
    CHECK(header_deliveries == 1 && read_calls == 0 && clock_ms == 0);
    CHECK(delivered_pieces == 1 && delivered_closing && last_flags == HOST_DATA_FAILED);
    CHECK(cleanups == 1 && !mutex_taken);

    /* Raw consumers accept the header probe and see their original failure sequence. */
    reset();
    response_status = 403;
    body_bytes = 1;
    complete_body = false;
    read_failure = -1;
    expire_read = true;
    CHECK(!poll_failure(false));
    CHECK(header_deliveries == 1 && read_calls > 0);
    CHECK(delivered_pieces == 1 && last_flags == HOST_DATA_FAILED && !delivered_closing);
    CHECK(delivered_status == HOST_FAILED_TIMEOUT);
}

static void test_retry_after(void)
{
    reset();
    response_status = 503;
    retry_header = "300";
    CHECK(!poll_failure(false));
    CHECK(cooldown_calls == 1 && cooldown_seconds == 300);

    reset();
    response_status = 429;
    retry_header = "invalid";
    read_failure = -1;
    CHECK(!poll_failure(false));
    CHECK(cooldown_calls == 1 && cooldown_seconds == 60);

    reset();
    response_status = 503;
    CHECK(!poll_failure(true));
    CHECK(cooldown_calls == 0);
}

static void test_pick_mime_and_lengths(void)
{
    const int64_t lengths[] = { -1, 0, INT32_MAX, (int64_t)INT32_MAX + 1, INT64_MAX };
    for (size_t index = 0; index < sizeof lengths / sizeof lengths[0]; index++) {
        reset();
        content_length_override = lengths[index];
        content_type = "text/html";
        response_status = 503;
        body_bytes = 5;
        CHECK(!poll_failure(true));
        CHECK(delivered_kind == APP_DELIVERY_VALUES && delivered_status == 503);
        CHECK(delivered_length == (lengths[index] <= INT32_MAX ? lengths[index] : -1));
    }
    reset();
    content_type = "text/plain";
    body_bytes = 0;
    CHECK(!poll_failure(true));
    CHECK(delivered_kind == APP_DELIVERY_VALUES && delivered_status == HOST_FAILED_NO_ANSWER);
}

static void test_retry_dates_and_raw_ownership(void)
{
    reset();
    wall_time = 1795780800;
    clock_reads = 0;
    request_t request = {0};
    esp_http_client_event_t event = { .event_id = HTTP_EVENT_ON_HEADER, .user_data = &request,
        .header_key = "Content-Type", .header_value = "application/json" };
    CHECK(on_http_event(&event) == ESP_OK);
    wall_time += 10;
    event.header_key = "Retry-After";
    event.header_value = "Fri, 27 Nov 2026 12:01:00 GMT";
    CHECK(on_http_event(&event) == ESP_OK);
    CHECK(request.response.retry_after == 60 && clock_reads == 1);
    reset();
    wall_time = 1795780800;
    response_status = 429;
    retry_header = "Fri, 27 Nov 2026 12:01:00 GMT";
    CHECK(!poll_failure(false));
    CHECK(cooldown_seconds == 60 && cooldown_calls == 1);
    reset();
    response_status = 429;
    retry_header = "300";
    poller_ask_t raw = { .number = HOST_REQUEST_FIRST, .method = HOST_HTTP_GET,
        .url = "https://example.test/file" };
    poller_net_request("probe", &raw);
    CHECK(cooldown_calls == 0);
}

static void test_certificate_admission(void)
{
    static const uint8_t der[] = {0x30, 3, 1, 2, 3};
    mbedtls_x509_crt certificate;
    mbedtls_x509_crt_init(&certificate);
    parsed_ca = true;
    CHECK(app_certificate_parse(&certificate, der, sizeof der));
    parsed_ca = false;
    CHECK(!app_certificate_parse(&certificate, der, sizeof der));
    parsed_ca = true;
    parsed_extra = true;
    CHECK(!app_certificate_parse(&certificate, der, sizeof der));
    parsed_extra = false;
    int before = certificate_parse_calls;
    static const uint8_t extra[] = {0x30, 3, 1, 2, 3, 0};
    CHECK(!app_certificate_parse(&certificate, extra, sizeof extra));
    CHECK(!app_certificate_parse(&certificate, der, sizeof der - 1));
    CHECK(certificate_parse_calls == before);
}

int main(void)
{
    test_certificate_admission();
    const poller_net_host_t host = { .deliver = deliver, .retry_after = record_retry_after };
    poller_net_start(&host);
    test_pick_mime_and_lengths();
    test_poll_memory_failures();
    test_poll_transport_failures();
    test_retry_after();
    test_retry_dates_and_raw_ownership();
    test_headers_refuse_slow_body();
    test_unwanted_rest_is_not_read();
    test_successful_poll_delivery();
    int status;
    reset(); CHECK(fetch(4096, &status)); CHECK(status == 200 && received == HOST_DATA_CHUNK_MAX + 17);
    reset(); read_failure = -1;
    CHECK(!fetch(4096, &status)); CHECK(status == HOST_FAILED_NO_ANSWER);
    CHECK(received == HOST_DATA_CHUNK_MAX && consumer_calls == 1);
    reset(); body_bytes = 17; read_failure = -1;
    CHECK(!fetch(4096, &status)); CHECK(status == HOST_FAILED_NO_ANSWER && consumer_calls == 0);
    reset(); read_failure = -1; expire_read = true;
    CHECK(!fetch(4096, &status)); CHECK(status == HOST_FAILED_TIMEOUT);
    reset(); consumer_accepts = false; read_failure = -1;
    CHECK(!fetch(4096, &status)); CHECK(status == 200 && consumer_calls == 1 && read_calls == 1);
    reset(); CHECK(!fetch(16, &status)); CHECK(status == 200 && consumer_calls == 0);
    reset(); response_status = 404;
    CHECK(!fetch(4096, &status)); CHECK(status == 404 && read_calls == 0);
    reset(); response_status = 503;
    CHECK(!fetch(4096, &status)); CHECK(status == 503 && read_calls == 0);
    reset(); fail_open = true;
    CHECK(!fetch(4096, &status)); CHECK(status == 0 && read_calls == 0);
    REPORT();
}
