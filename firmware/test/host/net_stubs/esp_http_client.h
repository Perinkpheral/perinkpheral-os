/* SPDX-License-Identifier: MIT */
#pragma once
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL (-1)
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_TIMEOUT 0x107
#define ESP_ERR_HTTP_EAGAIN 0x7007
#define ESP_ERROR_CHECK(result) assert((result) == ESP_OK)
const char *esp_err_to_name(esp_err_t error);

typedef void *esp_http_client_handle_t;
typedef enum {
    HTTP_METHOD_GET, HTTP_METHOD_POST, HTTP_METHOD_PUT, HTTP_METHOD_DELETE,
    HTTP_METHOD_PATCH, HTTP_METHOD_HEAD, HTTP_METHOD_PROPFIND, HTTP_METHOD_REPORT
} esp_http_client_method_t;
#define HTTP_EVENT_ON_HEADER 1
typedef struct {
    void *user_data;
    int event_id;
    char *header_key;
    char *header_value;
} esp_http_client_event_t;
typedef struct {
    const char *url;
    esp_http_client_method_t method;
    int timeout_ms;
    esp_err_t (*crt_bundle_attach)(void *);
    const char *user_agent;
    int buffer_size;
    int buffer_size_tx;
    esp_err_t (*event_handler)(esp_http_client_event_t *);
    void *user_data;
} esp_http_client_config_t;

esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *config);
esp_err_t esp_http_client_open(esp_http_client_handle_t client, int length);
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t client);
int esp_http_client_get_status_code(esp_http_client_handle_t client);
int esp_http_client_read(esp_http_client_handle_t client, char *bytes, int capacity);
typedef enum {
    HTTP_TRANSPORT_UNKNOWN,
    HTTP_TRANSPORT_OVER_TCP,
    HTTP_TRANSPORT_OVER_SSL,
} esp_http_client_transport_t;

esp_http_client_transport_t esp_http_client_get_transport_type(esp_http_client_handle_t client);
esp_err_t esp_http_client_set_redirection(esp_http_client_handle_t client);
esp_err_t esp_http_client_clear_response_buffer(esp_http_client_handle_t client);
esp_err_t esp_http_client_close(esp_http_client_handle_t client);
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t client);
int esp_http_client_get_socket(esp_http_client_handle_t client);
esp_err_t esp_http_client_get_url(esp_http_client_handle_t client, char *url, int capacity);
esp_err_t esp_http_client_set_header(esp_http_client_handle_t client, const char *name, const char *value);
esp_err_t esp_http_client_delete_header(esp_http_client_handle_t client, const char *name);
int esp_http_client_write(esp_http_client_handle_t client, const char *bytes, int length);
esp_err_t esp_http_client_set_timeout_ms(esp_http_client_handle_t client, int timeout);
bool esp_http_client_is_chunked_response(esp_http_client_handle_t client);
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t client);
int64_t esp_http_client_get_content_length(esp_http_client_handle_t client);
int esp_http_client_get_and_clear_last_tls_error(esp_http_client_handle_t client, int *error, int *flags);
int esp_http_client_get_errno(esp_http_client_handle_t client);
