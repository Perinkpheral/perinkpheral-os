/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "poller_net.h"
#include "app_certificate.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "lwip/sockets.h"
#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"

#include "app_kv.h"
#include "answer_headers.h"
#include "events.h"
#include "app_manifest.h"
#include "app_store.h"
#include "host_api.h"
#include "json_pick.h"
#include "storage.h"
#include "text_template.h"
#include "wifi.h"
#include "units.h"

/* The part of the poller that talks to the network: one request at a
 * time, on the poller's task. What it learns goes to the host through
 * the two calls it was given, and it knows nothing of schedules. */

static const char *TAG = "poller";

#define REQUEST_TIMEOUT_MS  15000
#define REQUEST_WITH_REDIRECTS_MS (2 * REQUEST_TIMEOUT_MS) /* the request and all it is sent on to */
#define ANSWER_MAX_BYTES    HOST_ANSWER_MAX
#define USER_AGENT          "perinkpheral"
#define HTTP_OK_FIRST       200
#define HTTP_OK_LAST        299
#define HTTP_TOO_MANY_REQUESTS 429
#define THROTTLE_DEFAULT_S   60

static const poller_net_host_t *s_host;

static int64_t now_ms(void)
{
    return esp_timer_get_time() / US_PER_MS;
}

/* ---- the time a request has, kept from outside it ----
 *
 * The client's own loops read for as long as bytes keep coming - headers
 * that never end, a body a byte every few seconds - and each read waits
 * only its own timeout, so a peer could keep the one request of the device
 * and the radio for ever. A timer shuts the connection's socket when the
 * time is up, and every read under way or to come fails. The socket is let
 * go before anything that may close it, so a number given to another
 * connection since is never shut. Opening - the TLS handshake - is not
 * covered: esp-tls gives no socket until the handshake has ended. */
static struct {
    SemaphoreHandle_t  lock;
    esp_timer_handle_t timer;
    int                socket; /* the open connection's; -1 for none */
    int64_t            by_ms;  /* only the poller's task sets it */
} s_cut = { .socket = -1 };

static void cut_if_late(void *unused)
{
    (void)unused;
    xSemaphoreTake(s_cut.lock, portMAX_DELAY);
    /* A timer that fired late for the request before finds a later time. */
    if (s_cut.socket >= 0 && now_ms() >= s_cut.by_ms) {
        shutdown(s_cut.socket, SHUT_RDWR);
        s_cut.socket = -1;
    }
    xSemaphoreGive(s_cut.lock);
}

/* The request in hand has until `by_ms`. */
static void cut_at(int64_t by_ms)
{
    esp_timer_stop(s_cut.timer);
    xSemaphoreTake(s_cut.lock, portMAX_DELAY);
    s_cut.by_ms = by_ms;
    xSemaphoreGive(s_cut.lock);
    int64_t in_ms = by_ms - now_ms();
    esp_timer_start_once(s_cut.timer, (uint64_t)(in_ms > 0 ? in_ms : 0) * US_PER_MS);
}

/* The client has a connection open: its socket is the one to shut. */
static void cut_watch(esp_http_client_handle_t client)
{
    int socket = esp_http_client_get_socket(client);
    xSemaphoreTake(s_cut.lock, portMAX_DELAY);
    s_cut.socket = socket;
    xSemaphoreGive(s_cut.lock);
    cut_if_late(NULL); /* the time ran out while it was being opened */
}

/* Before the client may close the connection. */
static void cut_forget(void)
{
    xSemaphoreTake(s_cut.lock, portMAX_DELAY);
    s_cut.socket = -1;
    xSemaphoreGive(s_cut.lock);
}

/* The request is over: nothing to shut, and no timer to wake a sleeping chip. */
static void cut_stop(void)
{
    cut_forget();
    esp_timer_stop(s_cut.timer);
}

static bool is_late(void)
{
    return now_ms() >= s_cut.by_ms;
}

void poller_net_start(const poller_net_host_t *host)
{
    s_host = host;
    s_cut.lock = xSemaphoreCreateMutex();
    configASSERT(s_cut.lock != NULL);
    const esp_timer_create_args_t timer = { .callback = cut_if_late, .name = "poll-cut" };
    ESP_ERROR_CHECK(esp_timer_create(&timer, &s_cut.timer));
}

static bool hand_over(const char *app_name, const app_delivery_t *delivery)
{
    return s_host->deliver(app_name, delivery);
}

/* The item of a list a poll with "each" goes over: -1 for any other. */
static int s_item = -1;

static void hand_over_values(const char *app_name, int request, int http_status,
                             const host_answer_info_t *response, const answer_headers_t *headers,
                             json_pick_t *picked)
{
    app_delivery_t delivery = { .kind = APP_DELIVERY_VALUES, .request = request,
                                .http_status = http_status, .response = response, .headers = headers,
                                .picked = picked, .has_item = s_item >= 0, .item = s_item };
    hand_over(app_name, &delivery);
}

static bool hand_over_piece(const char *app_name, int request, int http_status,
                            const host_answer_info_t *response, const answer_headers_t *headers,
                            const uint8_t *data, size_t length,
                            int flags)
{
    app_delivery_t delivery = { .kind = APP_DELIVERY_PIECE, .request = request,
                                .http_status = http_status, .response = response, .headers = headers,
                                .data = data, .length = length, .flags = flags,
                                .has_item = s_item >= 0, .item = s_item };
    return hand_over(app_name, &delivery);
}

/* The end of an answer nobody takes the rest of: for the delivery's own bookkeeping, given
 * only to the instance taking that answer - never to a new one as a failure of its own. */
static void close_unwanted_answer(const char *app_name, int request, const host_answer_info_t *response,
                                  const answer_headers_t *headers)
{
    app_delivery_t delivery = { .kind = APP_DELIVERY_PIECE, .request = request,
                                .http_status = HOST_FAILED_NO_ANSWER, .response = response,
                                .headers = headers, .flags = HOST_DATA_FAILED, .is_closing = true,
                                .has_item = s_item >= 0, .item = s_item };
    hand_over(app_name, &delivery);
}

/* The settings of an application are values in its own store; a poll's
 * {{key}} is one of them. */
bool poller_net_setting_of(void *context, const char *key, char *value, size_t value_size)
{
    const char *app_name = context;
    int32_t length = app_kv_get_or_default(app_name, key, value, (int32_t)value_size - 1);
    if (length < 0) {
        return false;
    }
    value[length] = '\0';
    return true;
}

/* Everything one request needs, in one block: none of it fits a stack. */
typedef struct {
    app_poll_t poll;
    char       url[APP_URL_MAX + 1];
    host_answer_info_t response; /* what the headers of the answer said */
    answer_headers_t headers;
    const char *paths[JSON_PICK_PATHS_MAX];
    json_pick_t pick;
    uint8_t     piece[HOST_DATA_CHUNK_MAX];
    uint8_t     next_piece[HOST_DATA_CHUNK_MAX];
    /* Whose settings fill the templates, and the item of the list a poll
     * with "each" stands for. */
    const char *app_name;
    char        item[APP_LIST_ITEM_MAX + 1];
    bool        network_failed;
} request_t;

/* A setting for a request's templates: the item, for the key of the list
 * the poll goes over; the stored value for any other. */
static bool request_value_of(void *context, const char *key, char *value, size_t value_size)
{
    const request_t *request = context;
    if (request->poll.each_key[0] != '\0' && strcmp(key, request->poll.each_key) == 0) {
        return strlcpy(value, request->item, value_size) < value_size;
    }
    return poller_net_setting_of((void *)request->app_name, key, value, value_size);
}

/* Reads poll number `poll_number` of the manifest into the request. */
static bool load_poll(request_t *request, const char *app_name, int poll_number)
{
    char *manifest = app_store_read_manifest(STORAGE_APPS_ROOT, app_name);
    bool  parsed = manifest != NULL && app_manifest_parse_poll(manifest, poll_number,
                                                               &request->poll) == APP_MANIFEST_OK;
    free(manifest);
    return parsed;
}

static const esp_http_client_method_t METHODS[] = {
    [HOST_HTTP_GET] = HTTP_METHOD_GET,
    [HOST_HTTP_POST] = HTTP_METHOD_POST,
    [HOST_HTTP_PUT] = HTTP_METHOD_PUT,
    [HOST_HTTP_DELETE] = HTTP_METHOD_DELETE,
    [HOST_HTTP_PATCH] = HTTP_METHOD_PATCH,
    [HOST_HTTP_HEAD] = HTTP_METHOD_HEAD,
    [HOST_HTTP_PROPFIND] = HTTP_METHOD_PROPFIND,
    [HOST_HTTP_REPORT] = HTTP_METHOD_REPORT,
};

static esp_err_t on_http_event(esp_http_client_event_t *event)
{
    request_t *request = event->user_data;
    host_answer_info_t *response = &request->response;
    if (event->event_id == HTTP_EVENT_ON_HEADER) {
        answer_info_header(response, event->header_key, event->header_value);
        answer_headers_add(&request->headers, event->header_key, event->header_value);
    }
    return ESP_OK;
}

/* ---- certificate authorities of an application's own ----
 *
 * An application may trust a root or two beside the browsers' set
 * ("certificates" in its manifest: files of its package), for its own
 * requests and nobody else's. They are read for the request under way and
 * let go after it; requests go one at a time for the whole device, so one
 * chain at a time is all there is. A chain they do not vouch for is still
 * the browsers' set's to judge: esp_crt_bundle's own check runs after them. */
static mbedtls_x509_crt s_trusted;
static bool             s_has_trusted;

static void trust_forget(void)
{
    if (s_has_trusted) {
        mbedtls_x509_crt_free(&s_trusted);
        s_has_trusted = false;
    }
}

static void trust_of(const char *app_name)
{
    trust_forget();
    char *manifest = app_store_read_manifest(STORAGE_APPS_ROOT, app_name);
    if (manifest == NULL) {
        return;
    }
    char files[MANIFEST_CERTIFICATES_MAX][MANIFEST_FILE_NAME_MAX + 1];
    int  count = app_manifest_certificates(manifest, files, MANIFEST_CERTIFICATES_MAX);
    free(manifest);
    if (count <= 0) {
        return;
    }
    mbedtls_x509_crt_init(&s_trusted);
    for (int i = 0; i < count; i++) {
        long size = app_store_file_size(STORAGE_APPS_ROOT, app_name, files[i]);
        /* PEM is read with its terminating zero, DER is not. */
        uint8_t *bytes = size > 0 && size <= MANIFEST_CERTIFICATE_MAX_BYTES ? malloc((size_t)size + 1) : NULL;
        if (bytes == NULL ||
            app_store_read_file(STORAGE_APPS_ROOT, app_name, files[i], 0, bytes, (size_t)size) != size) {
            ESP_LOGW(TAG, "%s: certificate %s cannot be read", app_name, files[i]);
            free(bytes);
            continue;
        }
        bytes[size] = '\0';
        mbedtls_x509_crt certificate;
        mbedtls_x509_crt_init(&certificate);
        bool valid = app_certificate_parse(&certificate, bytes, (size_t)size);
        free(bytes);
        if (valid) {
            valid = mbedtls_x509_crt_parse_der(&s_trusted, certificate.raw.p, certificate.raw.len) == 0;
        }
        mbedtls_x509_crt_free(&certificate);
        if (!valid) {
            ESP_LOGW(TAG, "%s: certificate %s is not exactly one CA", app_name, files[i]);
            continue;
        }
        s_has_trusted = true;
    }
    if (!s_has_trusted) {
        mbedtls_x509_crt_free(&s_trusted);
    }
}

/* The browsers' set, and beside it the application's own roots. */
static esp_err_t attach_trust(void *conf)
{
    esp_err_t err = esp_crt_bundle_attach(conf);
    if (err == ESP_OK && conf != NULL && s_has_trusted) {
        /* In place of the placeholder the set leaves as the chain: what these
         * vouch for passes, the rest is judged by the set's own check. */
        mbedtls_ssl_conf_ca_chain((mbedtls_ssl_config *)conf, &s_trusted, NULL);
    }
    return err;
}

/* The host loop is the sole writer of application KV values. Expanding
 * the URL, authorizing it and expanding headers and body in one call gives the
 * request one settings snapshot, including multi-key page saves. */
typedef struct {
    request_t *request;
    bool only_listed_hosts;
    int failure;
} preparation_t;

static void prepare_in_host_loop(void *argument)
{
    preparation_t *preparation = argument;
    request_t *request = preparation->request;
    char *manifest = preparation->only_listed_hosts
                         ? app_store_read_manifest(STORAGE_APPS_ROOT, request->app_name) : NULL;
    if (preparation->only_listed_hosts && manifest == NULL) {
        preparation->failure = HOST_FAILED_NOT_ALLOWED;
        return;
    }
    text_request_result_t result = text_template_prepare_request(&request->poll, manifest,
                                                                 request_value_of, request,
                                                                 request->url, sizeof request->url);
    free(manifest);
    preparation->failure = result == TEXT_REQUEST_READY ? 0
                           : result == TEXT_REQUEST_NOT_ALLOWED ? HOST_FAILED_NOT_ALLOWED
                           : result == TEXT_REQUEST_NO_MEMORY   ? HOST_FAILED_NO_MEMORY
                                                                : HOST_FAILED_NOT_READY;
}

static int prepare(request_t *request, const char *app_name, int method,
                    bool only_listed_hosts, esp_http_client_handle_t *client)
{
    request->app_name = app_name;
    preparation_t preparation = { .request = request, .only_listed_hosts = only_listed_hosts };
    events_call_patiently(prepare_in_host_loop, &preparation);
    if (preparation.failure != 0) {
        return preparation.failure;
    }
    esp_http_client_config_t config = {
        .url = request->url,
        .method = METHODS[method],
        .timeout_ms = REQUEST_TIMEOUT_MS,
        .crt_bundle_attach = attach_trust,
        .user_agent = USER_AGENT,
        .buffer_size = HOST_DATA_CHUNK_MAX,
        .event_handler = on_http_event,
        .user_data = request,
    };
    *client = esp_http_client_init(&config);
    if (*client == NULL) {
        return HOST_FAILED_NOT_READY;
    }
    for (int i = 0; i < request->poll.header_count; i++) {
        const app_header_t *header = &request->poll.headers[i];
        esp_http_client_set_header(*client, header->name, header->value);
    }
    return 0;
}

/* How long a whole answer may take to read, besides the 15 s each read may
 * wait: a server that sends a byte now and then would otherwise keep the
 * one connection of the device - and every other application's data - for
 * as long as it likes. Values are 256 KB at most, pieces 4 MB, a firmware
 * image 1.4 MB. The timer above holds it inside a read too. */
#define VALUES_WITHIN_MS   (60 * MS_PER_SECOND)
#define PIECES_WITHIN_MS   (5 * MS_PER_MINUTE)
#define FIRMWARE_WITHIN_MS (5 * MS_PER_MINUTE)
#define READ_TOO_LONG      (-1000) /* read_piece(): past the answer's time */

/* Up to a whole piece, however the network cuts it; less only at the end.
 * Negative when the connection broke, READ_TOO_LONG past the answer's time. */
static int read_piece(esp_http_client_handle_t client, uint8_t *piece)
{
    int length = 0;
    while (length < HOST_DATA_CHUNK_MAX) {
        /* Every read may wait its own timeout: the answer's time is looked at before each. */
        if (is_late()) {
            return READ_TOO_LONG;
        }
        int count = esp_http_client_read(client, (char *)piece + length,
                                         HOST_DATA_CHUNK_MAX - length);
        if (count < 0) {
            return is_late() ? READ_TOO_LONG : count;
        }
        if (count == 0) {
            break;
        }
        length += count;
    }
    return length;
}

/* The answer is read to its end, the connection closed, and only then
 * is the application called: off the screen it would have to be loaded
 * next to an open TLS session otherwise. */
static void fetch_values(request_t *request, esp_http_client_handle_t client,
                         const char *app_name, int poll_number, int http_status)
{
    int path_count = request->poll.pick_count;
    for (int i = 0; i < path_count; i++) {
        request->paths[i] = request->poll.pick[i];
    }
    json_pick_init(&request->pick, request->paths, path_count);
    /* Room for the elements of list paths, only when there are some. */
    bool wants_lists = false;
    for (int i = 0; i < path_count; i++) {
        wants_lists = wants_lists || json_pick_is_list(&request->pick, i);
    }
    char *list = wants_lists ? malloc(HOST_LIST_BYTES) : NULL;
    if (wants_lists && list == NULL) {
        /* Without the room every list would come empty, and pass for an answer. */
        cut_forget();
        esp_http_client_close(client);
        hand_over_values(app_name, poll_number, HOST_FAILED_NO_MEMORY, &request->response, &request->headers, &request->pick);
        return;
    }
    json_pick_lists(&request->pick, list, HOST_LIST_BYTES);
    size_t total = 0;
    int    length;
    bool   is_whole = true;
    cut_at(now_ms() + VALUES_WITHIN_MS);
    while ((length = read_piece(client, request->piece)) > 0) {
        total += (size_t)length;
        if (total > ANSWER_MAX_BYTES || !json_pick_feed(&request->pick, (char *)request->piece,
                                                        (size_t)length)) {
            is_whole = false;
            break;
        }
    }
    if (!json_pick_finish(&request->pick)) {
        is_whole = false; /* "{\"price\":123": values, and no document around them */
    }
    if (length == 0 && !esp_http_client_is_complete_data_received(client)) {
        length = -1;
    }
    request->network_failed = length < 0 && http_status >= HTTP_OK_FIRST && http_status <= HTTP_OK_LAST;
    cut_forget();
    esp_http_client_close(client);
    bool is_ok = http_status >= HTTP_OK_FIRST && http_status <= HTTP_OK_LAST;
    if ((length < 0 || !is_whole) && !is_ok) {
        /* A refusal - 401 with a login page, 404, 503 - is told as it is: its status is the
         * answer, and a page that is not whole JSON gives no values. */
        json_pick_init(&request->pick, request->paths, path_count);
    } else if (length < 0 || !is_whole) {
        /* Broke off, too long, or not JSON: what was read so far is not
         * an answer, and half of one must not pass for a whole one. */
        http_status = total > ANSWER_MAX_BYTES  ? HOST_FAILED_TOO_LARGE
                      : length == READ_TOO_LONG ? HOST_FAILED_TIMEOUT
                                                : HOST_FAILED_NO_ANSWER;
        if (length == READ_TOO_LONG) {
            ESP_LOGW(TAG, "%s/%d: the answer took over %d s", app_name, poll_number,
                     VALUES_WITHIN_MS / MS_PER_SECOND);
        }
    }
    if (json_pick_is_cut(&request->pick)) {
        ESP_LOGW(TAG, "%s/%d: list elements beyond %d bytes were dropped", app_name, poll_number,
                 HOST_LIST_BYTES);
    }
    hand_over_values(app_name, poll_number, http_status, &request->response, &request->headers, &request->pick);
    free(list);
}

/* The answer as it comes. A piece is handed over once the next one is in
 * hand, so that the last one can be told to be the last. */
static void fetch_pieces(request_t *request, esp_http_client_handle_t client,
                         const char *app_name, int poll_number, int http_status)
{
    if (http_status < HTTP_OK_FIRST || http_status > HTTP_OK_LAST) {
        app_delivery_t headers = { .kind = APP_DELIVERY_PIECE, .request = poll_number,
            .http_status = http_status, .response = &request->response, .headers = &request->headers,
            .flags = HOST_DATA_FIRST, .headers_only = true, .has_item = s_item >= 0, .item = s_item };
        if (!hand_over(app_name, &headers)) {
            close_unwanted_answer(app_name, poll_number, &request->response, &request->headers);
            return;
        }
    }
    uint8_t *piece = request->piece, *next = request->next_piece;
    cut_at(now_ms() + PIECES_WITHIN_MS);
    int      length = read_piece(client, piece);
    int      next_length = 0;
    int      flags = HOST_DATA_FIRST;
    size_t   total = 0;
    while (length >= 0) {
        total += (size_t)length;
        next_length = length == HOST_DATA_CHUNK_MAX ? read_piece(client, next) : 0;
        if (next_length < 0 || total > HOST_ANSWER_PIECES_MAX) {
            break;
        }
        if (next_length == 0) {
            if (!esp_http_client_is_complete_data_received(client)) {
                next_length = -1;
                break;
            }
            flags |= HOST_DATA_LAST;
        }
        bool is_rest_wanted = hand_over_piece(app_name, poll_number, http_status, &request->response,
                                              &request->headers, piece, (size_t)length, flags);
        if (flags & HOST_DATA_LAST) {
            return;
        }
        if (!is_rest_wanted) {
            /* Nobody takes the rest: the instance that took the beginning is gone, or reads
             * only a document whose result is decided already. The connection is the device's
             * only one; the polls of every application wait for it. */
            ESP_LOGI(TAG, "%s/%d: the rest of the answer is not wanted", app_name, poll_number);
            close_unwanted_answer(app_name, poll_number, &request->response, &request->headers);
            return;
        }
        flags = 0;
        uint8_t *swapped = piece;
        piece = next;
        next = swapped;
        length = next_length;
    }
    bool is_too_long = length == READ_TOO_LONG || next_length == READ_TOO_LONG;
    request->network_failed = (length < 0 || next_length < 0) &&
                              total <= HOST_ANSWER_PIECES_MAX &&
                              http_status >= HTTP_OK_FIRST && http_status <= HTTP_OK_LAST;
    if (is_too_long) {
        ESP_LOGW(TAG, "%s/%d: the answer took over %d s", app_name, poll_number,
                 PIECES_WITHIN_MS / MS_PER_SECOND);
    }
    hand_over_piece(app_name, poll_number,
                    total > HOST_ANSWER_PIECES_MAX ? HOST_FAILED_TOO_LARGE
                    : is_too_long                  ? HOST_FAILED_TIMEOUT
                                                   : HOST_FAILED_NO_ANSWER,
                    &request->response, &request->headers, NULL, 0, HOST_DATA_FAILED);
}

/* `why` is a HOST_FAILED_*, where the HTTP status would have been.
 * `only_on_screen`: not something to load a sleeping application for. */
static void tell_failure_to(const char *app_name, int poll_number, bool wants_values, int why,
                            bool only_on_screen)
{
    app_delivery_t delivery = { .request = poll_number, .http_status = why,
                                .only_on_screen = only_on_screen, .has_item = s_item >= 0,
                                .item = s_item };
    if (wants_values) {
        delivery.kind = APP_DELIVERY_VALUES;
    } else {
        delivery.kind = APP_DELIVERY_PIECE;
        delivery.flags = HOST_DATA_FAILED;
    }
    hand_over(app_name, &delivery);
}

static void tell_failure(const char *app_name, int poll_number, bool wants_values, int why)
{
    tell_failure_to(app_name, poll_number, wants_values, why, false);
}

#define REDIRECTS_MAX 3

static bool is_redirect(int http_status)
{
    return http_status == 301 || http_status == 302 || http_status == 303 ||
           http_status == 307 || http_status == 308;
}

/* Closes the connection on an answer nobody wants: what the client read
 * of its body along with the headers would otherwise come out in front of
 * the next answer. */
static void discard_answer(esp_http_client_handle_t client)
{
    cut_forget();
    esp_http_client_clear_response_buffer(client);
    esp_http_client_close(client);
}

typedef enum {
    REDIRECT_NONE,     /* the answer in hand is the answer */
    REDIRECT_FOLLOWED, /* the client is set to the new address, and closed */
    REDIRECT_REFUSED,  /* to a host the application may not ask */
} redirect_t;

/* Points the client where the answer in hand sends it. A request of the
 * application's own stays inside the manifest's "hosts"; and the headers
 * an application gave - a token, most likely - were meant for the host it
 * named. Remove them on a different host or a downgrade from TLS. */
static redirect_t follow_redirect(request_t *request, esp_http_client_handle_t client,
                                  const char *app_name, bool only_listed_hosts)
{
    bool from_https = esp_http_client_get_transport_type(client) == HTTP_TRANSPORT_OVER_SSL;
    cut_forget(); /* a redirect to another host closes the connection */
    if (esp_http_client_set_redirection(client) != ESP_OK) {
        cut_watch(client); /* its body is read as the answer */
        return REDIRECT_NONE; /* no Location, or one too long to read */
    }
    char *next_url = (char *)request->piece;
    if (esp_http_client_get_url(client, next_url, sizeof request->piece) != ESP_OK) {
        return REDIRECT_REFUSED;
    }
    if (only_listed_hosts && !app_manifest_is_same_host(request->url, next_url)) {
        char *manifest = app_store_read_manifest(STORAGE_APPS_ROOT, app_name);
        bool  allowed = manifest != NULL && app_manifest_allows_url(manifest, next_url, poller_net_setting_of,
                                                    (void *)app_name);
        free(manifest);
        if (!allowed) {
            return REDIRECT_REFUSED;
        }
    }
    if (!app_manifest_redirect_keeps_headers(request->url, next_url, from_https)) {
        for (int i = 0; i < request->poll.header_count; i++) {
            esp_http_client_delete_header(client, request->poll.headers[i].name);
        }
    }
    discard_answer(client);
    return REDIRECT_FOLLOWED;
}

/* Waits for the answer's headers. A server that takes the connection and
 * then says nothing is a timeout, which the client reports as "again". */
static esp_err_t headers_or_why_not(esp_http_client_handle_t client, request_t *request)
{
    /* Redirects may reuse a connection: reset per response, not per connection. */
    memset(&request->response, 0, sizeof request->response);
    answer_headers_reset(&request->headers);
    int64_t length = esp_http_client_fetch_headers(client);
    if (length == -ESP_ERR_HTTP_EAGAIN || (length < 0 && is_late())) {
        return ESP_ERR_TIMEOUT;
    }
    return length < 0 ? ESP_FAIL : ESP_OK;
}

/* mbedTLS's own numbers for a certificate that did not pass, without
 * their sign: MBEDTLS_ERR_X509_CERT_VERIFY_FAILED and
 * MBEDTLS_ERR_X509_FATAL_ERROR. */
#define X509_VERIFY_FAILED  0x2700
#define X509_NO_ROOT_FOR_IT 0x3000

/* mbedTLS can add a low-level error to a high-level one. An allocation
 * failure in either layer is local pressure, not a lost connection. */
#define TLS_HIGH_ERROR_MASK 0x7F80
#define TLS_LOW_ERROR_MASK  0x007F
#define SSL_ALLOC_FAILED    0x7F00
#define X509_ALLOC_FAILED   0x2880
#define PK_ALLOC_FAILED     0x3F80
#define PEM_ALLOC_FAILED    0x1180
#define CIPHER_ALLOC_FAILED 0x6180
#define MD_ALLOC_FAILED     0x5180
#define ECP_ALLOC_FAILED    0x4D80
#define DHM_ALLOC_FAILED    0x3400
#define MPI_ALLOC_FAILED    0x0010
#define ASN1_ALLOC_FAILED   0x006A

static bool tls_needs_memory(int tls_code)
{
    int low_error = tls_code & TLS_LOW_ERROR_MASK;
    if (low_error == MPI_ALLOC_FAILED || low_error == ASN1_ALLOC_FAILED) {
        return true;
    }
    switch (tls_code & TLS_HIGH_ERROR_MASK) {
    case SSL_ALLOC_FAILED:
    case X509_ALLOC_FAILED:
    case PK_ALLOC_FAILED:
    case PEM_ALLOC_FAILED:
    case CIPHER_ALLOC_FAILED:
    case MD_ALLOC_FAILED:
    case ECP_ALLOC_FAILED:
    case DHM_ALLOC_FAILED:
        return true;
    default:
        return false;
    }
}

/* Why a request that got no answer got none, as an application is told. */
static int why_no_answer(esp_http_client_handle_t client, esp_err_t err)
{
    int tls_error = 0, certificate_flags = 0;
    esp_err_t tls_failure = esp_http_client_get_and_clear_last_tls_error(client, &tls_error, &certificate_flags);
    int tls_code = tls_error < 0 ? -tls_error : tls_error;
    int socket_error = esp_http_client_get_errno(client);
    if (err == ESP_ERR_NO_MEM || tls_failure == ESP_ERR_NO_MEM ||
        socket_error == ENOMEM || socket_error == ENOBUFS || tls_needs_memory(tls_code)) {
        return HOST_FAILED_NO_MEMORY;
    }
    if (!wifi_is_online()) {
        return HOST_FAILED_NO_NETWORK;
    }
    /* The certificate the server showed was looked at and not accepted:
     * the flags say what was wrong with it, and the set of roots says
     * "fatal" when none of them signed it. Any other failed handshake is a
     * server that does not speak what the device does: no answer. */
    if (certificate_flags != 0 || tls_code == X509_VERIFY_FAILED ||
        tls_code == X509_NO_ROOT_FOR_IT) {
        return HOST_FAILED_NOT_TRUSTED;
    }
    if (err == ESP_ERR_TIMEOUT || socket_error == EAGAIN || socket_error == ETIMEDOUT) {
        return HOST_FAILED_TIMEOUT;
    }
    return HOST_FAILED_NO_ANSWER;
}

/* Makes the request that request->poll describes and hands the answer
 * to the application under `number`. `only_listed_hosts`: a request of
 * the application's own, which a redirect must not take out of its
 * manifest's "hosts". */
static void ask_network(request_t *request, const char *app_name, int number, int method,
                        const uint8_t *body, int body_length, bool only_listed_hosts);

static void perform(request_t *request, const char *app_name, int number, int method,
                    const uint8_t *body, int body_length, bool only_listed_hosts)
{
    trust_of(app_name);
    ask_network(request, app_name, number, method, body, body_length, only_listed_hosts);
    trust_forget();
}

static void ask_network(request_t *request, const char *app_name, int number, int method,
                        const uint8_t *body, int body_length, bool only_listed_hosts)
{
    bool wants_values = request->poll.pick_count > 0;
    esp_http_client_handle_t client = NULL;
    int preparation_failure = prepare(request, app_name, method, only_listed_hosts, &client);
    if (preparation_failure != 0) {
        /* Most likely a setting that is not there yet: the application is
         * told, so that it can say what it is waiting for. */
        ESP_LOGI(TAG, "%s/%d: cannot be asked yet", app_name, number);
        tell_failure(app_name, number, wants_values, preparation_failure);
        return;
    }
    int64_t   started_ms = now_ms();
    cut_at(started_ms + REQUEST_WITH_REDIRECTS_MS);
    if (!only_listed_hosts) {
        body = (const uint8_t *)request->poll.body;
        body_length = (int)strlen(request->poll.body);
    }
    esp_err_t err = esp_http_client_open(client, body_length);
    if (err == ESP_OK) {
        cut_watch(client);
    }
    if (err == ESP_OK && body_length > 0 &&
        esp_http_client_write(client, (const char *)body, body_length) != body_length) {
        err = ESP_FAIL;
    }
    int http_status = 0;
    if (err == ESP_OK) {
        err = headers_or_why_not(client, request);
        http_status = esp_http_client_get_status_code(client);
    }
    /* Only what can be asked again as it was: a body is not sent twice. */
    bool is_refused = false;
    for (int hop = 0; err == ESP_OK && method == HOST_HTTP_GET && is_redirect(http_status) &&
                      hop < REDIRECTS_MAX; hop++) {
        redirect_t redirect = follow_redirect(request, client, app_name, only_listed_hosts);
        if (redirect == REDIRECT_NONE) {
            break;
        }
        if (redirect == REDIRECT_REFUSED) {
            is_refused = true;
            break;
        }
        /* The hops share the time one request has: a chain of slow servers
         * is not waited for four times over, with the radio held. */
        int64_t left_ms = REQUEST_WITH_REDIRECTS_MS - (now_ms() - started_ms);
        if (left_ms <= 0) {
            err = ESP_ERR_TIMEOUT;
            break;
        }
        if (left_ms < REQUEST_TIMEOUT_MS) {
            esp_http_client_set_timeout_ms(client, (int)left_ms);
        }
        err = esp_http_client_open(client, 0);
        http_status = 0;
        if (err == ESP_OK) {
            cut_watch(client);
            err = headers_or_why_not(client, request);
            http_status = esp_http_client_get_status_code(client);
        }
    }
    if (is_refused) {
        ESP_LOGW(TAG, "%s/%d: redirected to a host its manifest does not list", app_name, number);
        tell_failure(app_name, number, wants_values, HOST_FAILED_NOT_ALLOWED);
    } else if (err != ESP_OK || http_status <= 0) {
        int why = why_no_answer(client, err);
        request->network_failed = why == HOST_FAILED_NO_NETWORK ||
                                  why == HOST_FAILED_NO_ANSWER || why == HOST_FAILED_TIMEOUT;
        /* A poll over a list is one number for every item: which of them did not answer. */
        ESP_LOGW(TAG, "%s/%d%s%s: no answer (%d): %s", app_name, number,
                 request->item[0] != '\0' ? " " : "", request->item, why, esp_err_to_name(err));
        tell_failure(app_name, number, wants_values, why);
    } else {
        int retry_after = request->response.retry_after;
        if (retry_after == 0 && http_status == HTTP_TOO_MANY_REQUESTS) {
            retry_after = THROTTLE_DEFAULT_S;
        }
        if (retry_after > 0) {
            s_host->retry_after(retry_after);
        }
        request->response.length = esp_http_client_is_chunked_response(client)
                                       ? -1
                                       : (int32_t)esp_http_client_get_content_length(client);
        wants_values = wants_values && answer_format(request->poll.response_format,&request->headers,&request->response)==HOST_FORMAT_JSON;
        if (method == HOST_HTTP_HEAD) {
            request->headers.head_response=true;
            /* There is no body to wait for: the headers are the answer. */
            if (wants_values) {
                for (int i = 0; i < request->poll.pick_count; i++) {
                    request->paths[i] = request->poll.pick[i];
                }
                json_pick_init(&request->pick, request->paths, request->poll.pick_count);
                hand_over_values(app_name, number, http_status, &request->response, &request->headers, &request->pick);
            } else {
                hand_over_piece(app_name, number, http_status, &request->response, &request->headers, request->piece,
                                0, HOST_DATA_FIRST | HOST_DATA_LAST);
            }
        } else if (wants_values) {
            fetch_values(request, client, app_name, number, http_status);
        } else {
            fetch_pieces(request, client, app_name, number, http_status);
        }
        bool is_ok = http_status >= HTTP_OK_FIRST && http_status <= HTTP_OK_LAST;
        ESP_LOG_LEVEL(is_ok ? ESP_LOG_INFO : ESP_LOG_WARN, TAG, "%s/%d: HTTP %d in %d ms",
                      app_name, number, http_status, (int)(now_ms() - started_ms));
    }
    cut_stop();
    esp_http_client_cleanup(client);
}

/* A poll that cannot be made now: the application on the screen is told
 * why, and one that is not is left asleep - there is nothing new. */
void poller_net_poll_not_made(const char *app_name, int poll_number, int why)
{
    request_t *request = calloc(1, sizeof *request);
    if (request == NULL) {
        return;
    }
    if (load_poll(request, app_name, poll_number)) {
        tell_failure_to(app_name, poll_number, request->poll.pick_count > 0, why, true);
    }
    free(request);
}

/* Item number `item` of a list setting, and how many there are. */
static int list_item(const char *app_name, const char *key, int item, char *text, size_t size)
{
    char list[APP_SETTING_VALUE_MAX + 1];
    text[0] = '\0';
    if (!poller_net_setting_of((void *)app_name, key, list, sizeof list) || list[0] == '\0') {
        return 0;
    }
    int count = 0;
    for (char *at = list;; count++) {
        char *end = strchr(at, '\n');
        if (end != NULL) {
            *end = '\0';
        }
        if (count == item) {
            strlcpy(text, at, size);
        }
        if (end == NULL) {
            return count + 1;
        }
        at = end + 1;
    }
}

int poller_net_poll(const char *app_name, int poll_number, int item, int *method,
                    bool *network_failed)
{
    *network_failed = false;
    request_t *request = calloc(1, sizeof *request);
    if (request == NULL) {
        ESP_LOGW(TAG, "%s/%d: no memory for the request", app_name, poll_number);
        return 0;
    }
    int items = 0;
    if (load_poll(request, app_name, poll_number)) {
        if (request->poll.each_key[0] != '\0') {
            items = list_item(app_name, request->poll.each_key, item, request->item,
                              sizeof request->item);
            /* An item another poll of the list claims by its prefix is not this one's:
             * passed over, and the round goes on. What stands for {{key}} is the item
             * without this poll's prefix. */
            const char *value;
            if (items > 0 && !app_manifest_poll_takes(&request->poll, request->item, &value)) {
                free(request);
                return items;
            }
            if (items > 0 && value != request->item) {
                memmove(request->item, value, strlen(value) + 1);
            }
        }
        /* An empty list leaves the key without a value: "not ready". */
        s_item = items > 0 ? item : -1;
        if (items == 0 && request->poll.each_key[0] != '\0') {
            request->poll.each_key[0] = '\0';
        }
        *method = request->poll.method;
        perform(request, app_name, poll_number, *method, NULL, 0, false);
        *network_failed = request->network_failed;
        s_item = -1;
    }
    free(request);
    return items;
}

void poller_net_request(const char *app_name, const poller_ask_t *ask)
{
    request_t *request = calloc(1, sizeof *request);
    if (request == NULL) {
        ESP_LOGW(TAG, "%s/%d: no memory for the request", app_name, ask->number);
        tell_failure(app_name, ask->number, false, HOST_FAILED_NO_ANSWER);
        return;
    }
    /* A request is a poll nobody scheduled: the same URL and headers. */
    strlcpy(request->poll.url, ask->url, sizeof request->poll.url);
    memcpy(request->poll.headers, ask->headers,
           (size_t)ask->header_count * sizeof request->poll.headers[0]);
    request->poll.header_count = ask->header_count;
    perform(request, app_name, ask->number, ask->method, ask->body, ask->body_length, true);
    free(request);
}

void poller_net_tell_failure(const char *app_name, int number, int why)
{
    tell_failure(app_name, number, false, why);
}

#define FETCH_HEADER_BYTES  4096
bool poller_net_fetch(const char *url, size_t max_bytes, poller_net_take_fn take, void *context,
                      int *http_status)
{
    *http_status = 0;
    static uint8_t piece[HOST_DATA_CHUNK_MAX]; /* only this task fetches */
    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = REQUEST_TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .user_agent = USER_AGENT,
        /* A file of a release is behind a redirect to an address a kilobyte
         * and more long, and a header has to fit here in one piece. */
        .buffer_size = FETCH_HEADER_BYTES,
        /* And the request for that address has to fit into this one. */
        .buffer_size_tx = FETCH_HEADER_BYTES,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        return false;
    }
    cut_at(now_ms() + REQUEST_WITH_REDIRECTS_MS);
    for (int hop = 0; hop <= REDIRECTS_MAX; hop++) {
        if (esp_http_client_open(client, 0) != ESP_OK) {
            break;
        }
        cut_watch(client);
        esp_http_client_fetch_headers(client);
        *http_status = esp_http_client_get_status_code(client);
        if (!is_redirect(*http_status)) {
            break;
        }
        cut_forget(); /* a redirect to another host closes the connection */
        if (esp_http_client_set_redirection(client) != ESP_OK) {
            break; /* a redirect is no file: nothing more is read */
        }
        discard_answer(client);
    }
    bool   is_ok = *http_status >= HTTP_OK_FIRST && *http_status <= HTTP_OK_LAST;
    size_t total = 0;
    int    length = 0;
    cut_at(now_ms() + FIRMWARE_WITHIN_MS);
    while (is_ok && (length = read_piece(client, piece)) > 0) {
        total += (size_t)length;
        if (total > max_bytes || !take(context, piece, (size_t)length)) {
            is_ok = false;
        }
    }
    if (is_ok && length < 0) {
        *http_status = length == READ_TOO_LONG ? HOST_FAILED_TIMEOUT : HOST_FAILED_NO_ANSWER;
    }
    cut_stop();
    esp_http_client_cleanup(client);
    return is_ok && length == 0;
}

