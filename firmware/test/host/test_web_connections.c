/* SPDX-License-Identifier: MIT */
#include "web_connections.h"
#include "tinytest.h"
#include <stdbool.h>

static int server;
static void (*queued_work)(void *);
static void *queued_argument;
static bool on_server, async_owned, shut[8], fail_queue, expect_shutdown;
static int completions;

esp_err_t httpd_queue_work(httpd_handle_t handle, void (*work)(void *), void *argument)
{
    CHECK(handle == &server);
    if (fail_queue) {
        return ESP_FAIL;
    }
    queued_work = work;
    queued_argument = argument;
    return ESP_OK;
}

esp_err_t httpd_get_client_list(httpd_handle_t handle, size_t *count, int *sockets)
{
    CHECK(on_server && handle == &server && *count >= 2);
    sockets[0] = 4;
    sockets[1] = 5;
    *count = 2;
    return ESP_OK;
}

int shutdown(int socket, int how)
{
    CHECK(how == 2 && socket >= 0 && socket < 8);
    shut[socket] = true;
    return 0;
}

int httpd_req_to_sockfd(httpd_req_t *request)
{
    CHECK(async_owned);
    return request->socket;
}

esp_err_t httpd_req_async_handler_complete(httpd_req_t *request)
{
    CHECK(async_owned && request->socket == 4);
    CHECK(shut[4] == expect_shutdown);
    async_owned = false;
    completions++;
    /* Model immediate session reuse as soon as ownership is released. */
    shut[4] = false;
    return ESP_OK;
}

int main(void)
{
    httpd_req_t request = { .handle = &server, .socket = 4 };
    async_owned = true;
    web_connections_shed(NULL);
    CHECK(queued_work == NULL);
    web_connections_shed(&server);
    CHECK(queued_work != NULL && !shut[4] && !shut[5]);
    on_server = true;
    queued_work(queued_argument);
    on_server = false;
    CHECK(shut[4] && shut[5] && async_owned);
    expect_shutdown = true;
    web_connections_complete(&request, ESP_FAIL);
    CHECK(completions == 1 && !async_owned && !shut[4]);

    async_owned = true;
    expect_shutdown = false;
    web_connections_complete(&request, ESP_OK);
    CHECK(completions == 2 && !shut[4]);

    async_owned = true;
    expect_shutdown = true;
    web_connections_complete(&request, ESP_FAIL);
    CHECK(completions == 3 && !shut[4]);
    fail_queue = true;
    queued_work = NULL;
    web_connections_shed(&server);
    CHECK(queued_work == NULL);
    REPORT();
}
