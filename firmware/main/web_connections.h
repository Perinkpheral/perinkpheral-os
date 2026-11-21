/* SPDX-License-Identifier: MIT */
#pragma once

#include "esp_http_server.h"

void web_connections_shed(httpd_handle_t server);
void web_connections_complete(httpd_req_t *request, esp_err_t result);
