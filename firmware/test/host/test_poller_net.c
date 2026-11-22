/* SPDX-License-Identifier: MIT */
/* The real HTTP fetcher with in-memory reads and no network sockets. */
#include "tinytest.h"
#include "freertos/task.h"
#include "../../main/poller_net.c"

static int response_status, read_failure, read_calls, cleanups, consumer_calls;
static size_t body_bytes, received;
static int64_t clock_ms;
static bool consumer_accepts, fail_open, expire_read;
static bool mutex_taken;

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
    CHECK_STR(config->url, "https://example.test/file"); return &response_status;
}
esp_err_t esp_http_client_open(esp_http_client_handle_t client, int length) { (void)client; CHECK(length == 0); return fail_open ? ESP_FAIL : ESP_OK; }
int esp_http_client_get_socket(esp_http_client_handle_t client) { (void)client; return -1; }
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t client) { (void)client; return (int64_t)body_bytes; }
int esp_http_client_get_status_code(esp_http_client_handle_t client) { (void)client; return response_status; }
int esp_http_client_read(esp_http_client_handle_t client, char *bytes, int capacity) {
    (void)client; read_calls++;
    if (body_bytes > 0) {
        size_t count = body_bytes < (size_t)capacity ? body_bytes : (size_t)capacity;
        memset(bytes, 'x', count); body_bytes -= count; return (int)count;
    }
    if (expire_read) clock_ms += FIRMWARE_WITHIN_MS;
    return read_failure;
}
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t client) { (void)client; cleanups++; CHECK(s_cut.socket == -1); return ESP_OK; }
esp_err_t esp_http_client_set_redirection(esp_http_client_handle_t client) { (void)client; return ESP_FAIL; }
esp_err_t esp_http_client_clear_response_buffer(esp_http_client_handle_t client) { (void)client; return ESP_OK; }
esp_err_t esp_http_client_close(esp_http_client_handle_t client) { (void)client; return ESP_OK; }
esp_err_t esp_http_client_get_url(esp_http_client_handle_t client, char *url, int capacity) { (void)client; (void)url; (void)capacity; return ESP_FAIL; }
esp_err_t esp_http_client_set_header(esp_http_client_handle_t client, const char *name, const char *value) { (void)client; (void)name; (void)value; return ESP_OK; }
esp_err_t esp_http_client_delete_header(esp_http_client_handle_t client, const char *name) { (void)client; (void)name; return ESP_OK; }
int esp_http_client_write(esp_http_client_handle_t client, const char *bytes, int length) { (void)client; (void)bytes; return length; }
esp_err_t esp_http_client_set_timeout_ms(esp_http_client_handle_t client, int timeout) { (void)client; (void)timeout; return ESP_OK; }
bool esp_http_client_is_chunked_response(esp_http_client_handle_t client) { (void)client; return false; }
int64_t esp_http_client_get_content_length(esp_http_client_handle_t client) { (void)client; return (int64_t)body_bytes; }
int esp_http_client_get_and_clear_last_tls_error(esp_http_client_handle_t client, int *error, int *flags) { (void)client; *error = *flags = 0; return ESP_OK; }
int esp_http_client_get_errno(esp_http_client_handle_t client) { (void)client; return 0; }
void mbedtls_x509_crt_init(mbedtls_x509_crt *chain) { (void)chain; }
void mbedtls_x509_crt_free(mbedtls_x509_crt *chain) { (void)chain; }
int mbedtls_x509_crt_parse(mbedtls_x509_crt *chain, const unsigned char *bytes, size_t length) { (void)chain; (void)bytes; (void)length; return -1; }
void mbedtls_ssl_conf_ca_chain(mbedtls_ssl_config *config, mbedtls_x509_crt *chain, void *revocations) { (void)config; (void)chain; (void)revocations; }
int32_t app_kv_get_or_default(const char *name, const char *key, void *value, int32_t capacity) { (void)name; (void)key; (void)value; (void)capacity; return HOST_ERR_NOT_FOUND; }
char *app_store_read_manifest(const char *root, const char *name) { (void)root; (void)name; return NULL; }
long app_store_file_size(const char *root, const char *name, const char *file) { (void)root; (void)name; (void)file; return -1; }
long app_store_read_file(const char *root, const char *name, const char *file, long offset, void *bytes, size_t capacity) { (void)root; (void)name; (void)file; (void)offset; (void)bytes; (void)capacity; return -1; }
void events_call_patiently(events_call_fn run, void *argument) { run(argument); }
bool wifi_is_online(void) { return true; }

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

int main(void)
{
    poller_net_start(NULL);
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
