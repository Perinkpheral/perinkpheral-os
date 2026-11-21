/* SPDX-License-Identifier: MIT */
#pragma once
#include <stddef.h>

typedef int esp_err_t;
typedef void *httpd_handle_t;
typedef struct {
    httpd_handle_t handle;
    int socket;
} httpd_req_t;
#define ESP_OK 0
#define ESP_FAIL -1

esp_err_t httpd_queue_work(httpd_handle_t server, void (*work)(void *), void *argument);
esp_err_t httpd_get_client_list(httpd_handle_t server, size_t *count, int *sockets);
int httpd_req_to_sockfd(httpd_req_t *request);
esp_err_t httpd_req_async_handler_complete(httpd_req_t *request);
