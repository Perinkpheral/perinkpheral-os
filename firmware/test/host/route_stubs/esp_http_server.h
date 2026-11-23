/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>
#include "esp_err.h"
#include "freertos/task.h"
typedef void *httpd_handle_t;
typedef enum { HTTP_GET, HTTP_POST } httpd_method_t;
typedef int httpd_err_code_t;
typedef struct {
    httpd_handle_t handle;
    httpd_method_t method;
    void *user_ctx;
    size_t content_len;
    const char *uri;
} httpd_req_t;
typedef struct {
    const char *uri;
    httpd_method_t method;
    esp_err_t (*handler)(httpd_req_t *);
    void *user_ctx;
} httpd_uri_t;
typedef struct {
    int recv_wait_timeout, stack_size, task_priority, max_open_sockets, max_uri_handlers, server_port;
    bool lru_purge_enable;
} httpd_config_t;
#define HTTPD_DEFAULT_CONFIG() ((httpd_config_t){0})
#define HTTPD_404_NOT_FOUND 404
#define ESP_ERROR_CHECK(result) ((void)(result))
esp_err_t httpd_req_get_hdr_value_str(httpd_req_t *, const char *, char *, size_t);
esp_err_t httpd_resp_set_status(httpd_req_t *, const char *);
esp_err_t httpd_resp_set_type(httpd_req_t *, const char *);
esp_err_t httpd_resp_set_hdr(httpd_req_t *, const char *, const char *);
esp_err_t httpd_resp_send(httpd_req_t *, const char *, ssize_t);
esp_err_t httpd_resp_sendstr(httpd_req_t *, const char *);
esp_err_t httpd_resp_send_500(httpd_req_t *);
esp_err_t httpd_resp_send_err(httpd_req_t *, int, const char *);
esp_err_t httpd_req_async_handler_begin(httpd_req_t *, httpd_req_t **);
esp_err_t httpd_req_async_handler_complete(httpd_req_t *);
esp_err_t httpd_req_get_url_query_str(httpd_req_t *, char *, size_t);
esp_err_t httpd_query_key_value(const char *, const char *, char *, size_t);
esp_err_t httpd_register_uri_handler(httpd_handle_t, const httpd_uri_t *);
esp_err_t httpd_register_err_handler(httpd_handle_t, int, esp_err_t (*)(httpd_req_t *, int));
esp_err_t httpd_start(httpd_handle_t *, const httpd_config_t *);
int httpd_req_recv(httpd_req_t *, char *, size_t);

esp_err_t httpd_resp_send_chunk(httpd_req_t *, const char *, ssize_t);
esp_err_t httpd_resp_sendstr_chunk(httpd_req_t *, const char *);
