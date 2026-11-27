/* SPDX-License-Identifier: MIT */
#include "tinytest.h"
#include "standin.h"
#include "manifest_limits.h"
#include <unistd.h>
#define main capability_main
#include "../../../sdk/check/checks.c"
#undef main

static const char *const picks[] = { "value" };
static const standin_poll_t polls[] = {
    { .url = "https://example.test/{{items}}", .picks = picks, .pick_count = 1, .each = "items" },
    { .url = "https://example.test/{{items}}", .each = "items", .prefix = "x:" },
    { .url = "https://example.test/fixed", .each = "items" },
};
static const char *const hosts[] = { "example.test" };
static standin_answer_t answers[66];
const standin_subject_t standin_subject = {
    .polls = polls, .poll_count = 3, .hosts = hosts, .host_count = 1,
    .answers = answers, .answer_count = 66,
};
void standin_subject_handlers(void) {}

static int32_t raw_bytes, raw_last, raw_failed, document_status, document_length, picked_status;
static uint8_t document[32];
static bool queue_during_callback;

static void on_data(int32_t request, const uint8_t *data, int32_t length, int32_t flags, int32_t status)
{
    (void)data;
    (void)request;
    raw_bytes += length;
    raw_last += (flags & HOST_DATA_LAST) != 0;
    if (flags & HOST_DATA_FAILED) raw_failed = status;
    if (queue_during_callback) {
        queue_during_callback = false;
        const char url[] = "https://example.test/";
        CHECK(http_send(HOST_REQUEST_LAST, HOST_HTTP_GET, url, sizeof url - 1, NULL, 0, NULL, 0) == HOST_OK);
    }
}
static void on_document(int32_t request, int32_t format, int32_t status, int32_t length)
{
    (void)request;
    CHECK(format == HOST_FORMAT_JSON);
    document_status = status;
    document_length = length;
    host_answer_info_t info;
    CHECK(answer_info(&info, sizeof info) == sizeof info);
    CHECK(info.status == 200 && info.length == -1);
    CHECK(answer_item() == 7);
    CHECK(document_buffer_set(document, sizeof document) == HOST_ERR_ARGUMENT);
}
static void on_picked(int32_t request, int32_t status)
{
    (void)request;
    picked_status = status;
    if (status == 503) CHECK(answer_count(0) == 0);
}
static void reset_network(void)
{
    standin_reset();
    standin_handlers = (standin_handlers_t){ .on_data = on_data, .on_picked = on_picked };
    raw_bytes = raw_last = raw_failed = document_status = document_length = picked_status = 0;
}
static void test_request_arguments(void)
{
    reset_network();
    const char url[] = "https://example.test/";
    const int32_t tags[] = { HOST_REQUEST_FIRST, 255, 256, INT32_MAX - 1, INT32_MAX };
    for (size_t index = 0; index < sizeof tags / sizeof tags[0]; index++) {
        CHECK(http_send(tags[index], HOST_HTTP_GET, url, sizeof url - 1, NULL, 0, NULL, 0) == HOST_OK);
        CHECK(standin()->requests[index].tag == tags[index]);
    }
    CHECK(http_send(INT32_MIN, HOST_HTTP_GET, url, sizeof url - 1, NULL, 0, NULL, 0) == HOST_ERR_ARGUMENT);
    CHECK(http_send(15, HOST_HTTP_GET, url, sizeof url - 1, NULL, 0, NULL, 0) == HOST_ERR_ARGUMENT);
    CHECK(http_send(16, HOST_HTTP_GET, url, sizeof url - 1, NULL, 0, "x", 1) == HOST_ERR_ARGUMENT);
    CHECK(http_send(16, HOST_HTTP_HEAD, url, sizeof url - 1, NULL, 0, "x", 1) == HOST_ERR_ARGUMENT);
    CHECK(http_send(16, HOST_HTTP_POST, url, sizeof url - 1, NULL, 0, "x", 1) == HOST_OK);
    const char *invalid[] = { "http://example.test:/", "http://example.test:0/", "http://example.test:65536/" };
    for (size_t index = 0; index < sizeof invalid / sizeof invalid[0]; index++) CHECK(!standin_url_allowed(invalid[index]));
    CHECK(standin_url_allowed("http://example.test/"));
    CHECK(standin_url_allowed("http://example.test:80/"));
}
static void test_head_and_document_metadata(void)
{
    reset_network();
    standin_handlers.on_document = on_document;
    const char url[] = "https://example.test/";
    CHECK(http_send(INT32_MAX, HOST_HTTP_HEAD, url, sizeof url - 1, NULL, 0, NULL, 0) == HOST_OK);
    CHECK(http_send(INT32_MAX, HOST_HTTP_GET, url, sizeof url - 1, NULL, 0, NULL, 0) == HOST_OK);
    CHECK(document_buffer_set(document, sizeof document) == HOST_OK);
    const standin_pair_t headers[] = { { "Content-Type", "application/json" }, { "Content-Length", "2147483648" } };
    standin_answer_headers(headers, 2);
    standin_answer_method(HOST_HTTP_HEAD);
    queue_during_callback = true;
    standin_data(INT32_MAX, 7, 200, "unwanted", 8, 1, STANDIN_WHOLE, 0);
    CHECK(raw_bytes == 0 && raw_last == 1 && document_status == 200 && document_length == 0);
    standin_answer_headers(headers, 2);
    standin_answer_method(HOST_HTTP_GET);
    standin_data(INT32_MAX, 7, 200, "{}", 2, 1, STANDIN_WHOLE, 0);
    CHECK(raw_bytes == 2 && document_status == 200 && document_length == 2);
    standin_answer_headers(headers, 2);
    standin_data(INT32_MAX, 7, 200, "{", 1, 1, STANDIN_WHOLE, 0);
    CHECK(document_status == HOST_FAILED_NO_ANSWER && document_length == 0);
    host_answer_info_t info;
    CHECK(answer_info(&info, sizeof info) == HOST_ERR_NOT_FOUND && answer_item() == -1);
}
static void test_pick_route_and_limits(void)
{
    reset_network();
    const standin_pair_t header = { "Content-Type", "text/html" };
    standin_answer_headers(&header, 1);
    standin_picked(0, -1, 503, "<html>", 6);
    CHECK(picked_status == 503 && raw_bytes == 0);
    standin_picked(0, -1, 200, "{}{}", 4);
    CHECK(picked_status == HOST_FAILED_NO_ANSWER);
    uint8_t *bytes = malloc(HOST_ANSWER_PIECES_MAX + 1);
    CHECK(bytes != NULL);
    memset(bytes, ' ', HOST_ANSWER_PIECES_MAX + 1);
    memcpy(bytes, "{}", 2);
    standin_picked(0, -1, 200, bytes, HOST_ANSWER_MAX);
    CHECK(picked_status == 200);
    standin_picked(0, -1, 200, bytes, HOST_ANSWER_MAX + 1);
    CHECK(picked_status == HOST_FAILED_TOO_LARGE);
    standin_data(INT32_MAX, -1, 200, bytes, HOST_ANSWER_MAX + 1, 1024, STANDIN_WHOLE, 0);
    CHECK(raw_bytes == HOST_ANSWER_MAX + 1 && raw_last == 1);
    raw_bytes = raw_last = 0;
    standin_data(INT32_MAX, -1, 200, bytes, HOST_ANSWER_PIECES_MAX + 1, 1024, STANDIN_WHOLE, 0);
    CHECK(raw_failed == HOST_FAILED_TOO_LARGE && raw_last == 0 && raw_bytes <= HOST_ANSWER_PIECES_MAX);
    free(bytes);
}
static void test_list_preparation(void)
{
    reset_network();
    standin_set("items", "a\n\nx:\nx:b\nz\n");
    CHECK(standin_poll_items(0) == 4);
    CHECK(standin_poll_item(0, 0) == 0);
    CHECK(standin_poll_item(0, 1) == 1);
    CHECK(standin_poll_item(0, 2) == 4);
    CHECK(standin_poll_item(0, 3) == 5);
    CHECK(standin_poll_items(1) == 1 && standin_poll_item(1, 0) == 3);
    char url[512];
    CHECK(standin_poll_url(1, 3, url, sizeof url));
    CHECK_STR(url, "https://example.test/b");
    standin_set("items", "");
    CHECK(standin_poll_list_is_empty(2));
    CHECK(standin_poll_url(2, -1, url, sizeof url) && standin_poll_ready(2, -1));
    char long_list[MANIFEST_SETTING_VALUE_MAX + 2];
    memset(long_list, 'a', sizeof long_list - 1);
    long_list[sizeof long_list - 1] = '\0';
    standin_set("items", long_list);
    CHECK(standin_poll_list_is_empty(0));
    char long_item[MANIFEST_LIST_ITEM_MAX + 12];
    memset(long_item, 'a', sizeof long_item - 1);
    long_item[sizeof long_item - 1] = '\0';
    standin_set("items", long_item);
    CHECK(standin_poll_url(0, 0, url, sizeof url));
    CHECK(strlen(url) == strlen("https://example.test/") + MANIFEST_LIST_ITEM_MAX);
    CHECK(!standin_fill_url("https://example.test/{{bad key}}", url, sizeof url));
    char template_value[MANIFEST_SETTING_VALUE_MAX + 2];
    memset(template_value, 'a', sizeof template_value - 1);
    template_value[sizeof template_value - 1] = '\0';
    template_value[1] = '\n';
    standin_set("value", template_value);
    CHECK(!standin_fill_url("https://example.test/{{value}}", url, sizeof url));
    template_value[1] = 'a';
    standin_set("value", template_value);
    const char header_template[] = "X-Value: {{value}}";
    CHECK(!standin_header_lines_ready(header_template, sizeof header_template - 1));
    template_value[MANIFEST_SETTING_VALUE_MAX] = '\0';
    standin_set("value", template_value);
    CHECK(standin_header_lines_ready(header_template, sizeof header_template - 1));

}
static void test_complete_fixtures(void)
{
    char path[] = "/tmp/perinkpheral-fixture-XXXXXX";
    int descriptor = mkstemp(path);
    CHECK(descriptor >= 0);
    FILE *file = fdopen(descriptor, "wb");
    CHECK(file != NULL);
    uint8_t *body = malloc(HOST_ANSWER_MAX + 27);
    CHECK(body != NULL);
    memset(body, 'x', HOST_ANSWER_MAX + 27);
    CHECK(fwrite(body, 1, HOST_ANSWER_MAX + 27, file) == HOST_ANSWER_MAX + 27);
    free(body);
    CHECK(fclose(file) == 0);
    for (size_t index = 0; index < sizeof answers / sizeof answers[0]; index++) {
        answers[index] = (standin_answer_t){ .address = "other", .body = "" };
    }
    answers[65] = (standin_answer_t){ .address = "last", .file = path, .status = 200 };
    answer_t answer;
    CHECK(answer_for("last", &answer));
    CHECK(answer.length == HOST_ANSWER_MAX + 27);
    CHECK(answer.body[HOST_ANSWER_MAX + 26] == 'x');
    unlink(path);
}
int main(void)
{
    test_request_arguments();
    test_head_and_document_metadata();
    test_pick_route_and_limits();
    test_list_preparation();
    test_complete_fixtures();
    REPORT();
}
