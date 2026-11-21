/* SPDX-License-Identifier: MIT */
#include "web_connections.h"

#include "lwip/sockets.h"

#define CONNECTIONS_MAX 8

static void shut_connections(void *argument)
{
    httpd_handle_t server = argument;
    int sockets[CONNECTIONS_MAX];
    size_t count = CONNECTIONS_MAX;
    if (httpd_get_client_list(server, &count, sockets) != ESP_OK) {
        return;
    }
    for (size_t index = 0; index < count; index++) {
        /* Shutdown wakes I/O but leaves an async session owned until completion. */
        shutdown(sockets[index], SHUT_RDWR);
    }
}

void web_connections_shed(httpd_handle_t server)
{
    if (server != NULL) {
        /* Enumerate on the server task so a socket cannot be reused meanwhile. */
        httpd_queue_work(server, shut_connections, server);
    }
}

void web_connections_complete(httpd_req_t *request, esp_err_t result)
{
    if (result != ESP_OK) {
        /* After completion the server may already have reused this socket. */
        shutdown(httpd_req_to_sockfd(request), SHUT_RDWR);
    }
    httpd_req_async_handler_complete(request);
}
