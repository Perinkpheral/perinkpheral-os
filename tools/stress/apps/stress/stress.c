/* SPDX-License-Identifier: MIT */
#include "host_api.h"
#include "app_alloc.h"
#include "app_text.h"
#include <stddef.h>

void *memset(void *destination, int value, size_t length);

#ifndef STRESS_ARENA
#define STRESS_ARENA 1024
#endif
APP_ALLOC_ARENA(STRESS_ARENA);

#define REQUEST_ATTEMPTS 20
#define FILE_ATTEMPTS 64
#define STORE_ATTEMPTS 40
#define FILE_BYTES 4096
#define STORE_BYTES 200
#define RECURSION_DEPTH 100000

static char mode[32];
static uint8_t document[HOST_DOCUMENT_MAX];
static int32_t answers[4];
static int32_t last_status[4];
static uint32_t requests;
static uint32_t refusals;
static uint32_t ticks;
static volatile uint32_t sink;

#ifdef STRESS_BIG
#define PAYLOAD_256 "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef" \
                    "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef" \
                    "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef" \
                    "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
#define PAYLOAD_1K PAYLOAD_256 PAYLOAD_256 PAYLOAD_256 PAYLOAD_256
#define PAYLOAD_4K PAYLOAD_1K PAYLOAD_1K PAYLOAD_1K PAYLOAD_1K
#define PAYLOAD_16K PAYLOAD_4K PAYLOAD_4K PAYLOAD_4K PAYLOAD_4K
static const uint8_t payload[] = PAYLOAD_16K PAYLOAD_16K PAYLOAD_4K PAYLOAD_1K PAYLOAD_1K;
#endif

static int mode_is(const char *expected)
{
    const char *current = mode;
    while (*current && *current == *expected) {
        current++;
        expected++;
    }
    return *current == *expected;
}

static void read_mode(void)
{
    memset(mode, 0, sizeof mode);
    if (kv_get("mode", 4, mode, sizeof mode - 1) < 0) {
        mode[0] = 'i'; mode[1] = 'd'; mode[2] = 'l'; mode[3] = 'e';
    }
}

static int32_t recurse(int32_t depth)
{
    volatile char stack_bytes[64];
    stack_bytes[0] = (char)depth;
    return depth <= 0 ? stack_bytes[0] : recurse(depth - 1) + stack_bytes[0];
}

#ifndef STRESS_DOCUMENT_ONLY
static void queue_requests(void)
{
    char url[256];
    int32_t length = kv_get("server", 6, url, sizeof url - 3);
    if (length <= 0 || length > (int32_t)sizeof url - 3) return;
    url[length++] = '/';
    url[length++] = 'q';
    for (int32_t index = 0; index < REQUEST_ATTEMPTS; index++) {
        int32_t result = http_send(HOST_REQUEST_FIRST + requests++ % 200, HOST_HTTP_POST,
                                  url, length, "X-A: 1", 6, document, HOST_HTTP_BODY_MAX);
        if (result != HOST_OK) { refusals++; break; }
    }
}
#endif

static void fill_files(void)
{
    char name[] = "f00";
    for (int32_t index = 0; index < FILE_ATTEMPTS; index++) {
        name[1] = (char)('0' + index / 10);
        name[2] = (char)('0' + index % 10);
        if (file_write(name, 3, 0, document, FILE_BYTES) < 0) { refusals++; break; }
        requests++;
    }
}

static void fill_store(void)
{
    char key[] = "k00";
    for (int32_t index = 0; index < STORE_ATTEMPTS; index++) {
        key[1] = (char)('0' + index / 10);
        key[2] = (char)('0' + index % 10);
        if (kv_set(key, 3, document, STORE_BYTES) < 0) { refusals++; break; }
        requests++;
    }
}

static void act(void)
{
    read_mode();
#ifdef STRESS_BIG
    sink += payload[sink % (sizeof payload - 1)];
#endif
    if (mode_is("loop")) { for (;;) sink++; }
    if (mode_is("recurse")) sink = (uint32_t)recurse(RECURSION_DEPTH);
    if (mode_is("arena")) {
        uint8_t *allocation = app_alloc(STRESS_ARENA - 64);
        if (allocation) memset(allocation, 1, STRESS_ARENA - 64);
        else refusals++;
    }
#ifndef STRESS_DOCUMENT_ONLY
    if (mode_is("http")) queue_requests();
#endif
    if (mode_is("files")) fill_files();
    if (mode_is("kv")) fill_store();
    if (mode_is("timers")) timer_set(1, 1);
    if (mode_is("redraw")) timer_set(2, 1);
    if (mode_is("trap")) {
        volatile int32_t *outside_memory = (int32_t *)0x7ffffff0;
        sink = (uint32_t)*outside_memory;
    }
    screen_redraw(HOST_REDRAW_NORMAL);
}

void app_on_load(void) { document_buffer_set(document, sizeof document); read_mode(); }
void app_on_focus(void) { act(); }
void app_on_settings(void) { act(); }
void app_on_intent(int32_t intent) { (void)intent; act(); }
void app_on_button(int32_t event) { if (event == BTN_OK_PRESSED) act(); }

void app_on_timer(int32_t id)
{
    ticks++;
    if (id == 1 && mode_is("timers")) timer_set(1, 1);
    if (id == 2 && mode_is("redraw")) {
        screen_redraw(HOST_REDRAW_NORMAL);
        timer_set(2, 1);
    }
}

#ifndef STRESS_DOCUMENT_ONLY
void app_on_data(int32_t request, const uint8_t *data, int32_t length, int32_t flags, int32_t status)
{
    (void)data; (void)length;
    if (mode_is("dataloop")) { for (;;) sink++; }
    if (request >= 0 && request < 4) { answers[request]++; last_status[request] = status; }
    if (request >= HOST_REQUEST_FIRST && (flags & (HOST_DATA_LAST | HOST_DATA_FAILED)) && mode_is("http")) {
        queue_requests();
    }
}
#endif

void app_on_picked(int32_t request, int32_t status)
{
    char value[64];
    (void)request;
    answers[0]++;
    last_status[0] = status;
    for (int32_t index = 0; index < 4; index++) sink += (uint32_t)answer_value(index, value, sizeof value);
    sink += (uint32_t)(answer_count(1) + answer_count(2));
}

void app_on_document(int32_t request, int32_t format, int32_t status, int32_t length)
{
    (void)format; (void)length;
    if (request >= 0 && request < 4) { answers[request]++; last_status[request] = status; }
}

void app_on_draw(void)
{
    char text[96];
    app_text_t line;
    int32_t height = gfx_font_height(HOST_FONT_TEXT);
    gfx_draw_text(HOST_UI_MARGIN, HOST_UI_MARGIN, HOST_FONT_TEXT, mode, app_text_length(mode));
    for (int32_t index = 0; index < 4; index++) {
        app_text_start(&line, text, sizeof text);
        app_text_add(&line, "poll "); app_text_add_number(&line, index);
        app_text_add(&line, " "); app_text_add_number(&line, answers[index]);
        app_text_add(&line, " / "); app_text_add_number(&line, last_status[index]);
        gfx_draw_text(HOST_UI_MARGIN, HOST_UI_MARGIN + (index + 1) * height, HOST_FONT_TEXT, text, app_text_length(text));
    }
    app_text_start(&line, text, sizeof text);
    app_text_add(&line, "ticks "); app_text_add_number(&line, (int32_t)ticks);
    app_text_add(&line, " requests "); app_text_add_number(&line, (int32_t)requests);
    app_text_add(&line, " refused "); app_text_add_number(&line, (int32_t)refusals);
    gfx_draw_text(HOST_UI_MARGIN, HOST_UI_MARGIN + 5 * height, HOST_FONT_SMALL, text, app_text_length(text));
}
