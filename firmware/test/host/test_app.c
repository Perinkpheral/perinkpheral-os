/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "app.h"
#include "answer_headers.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#include "host_api.h"
#include "budget_wasm.h"
#include "document_wasm.h"
#include "memory_wasm.h"
#include "every_call_wasm.h"
#include "probe_wasm.h"
#include "frozen_probe_wasm.h"
#include "wrongarg_wasm.h"
#include "wrongret_wasm.h"
#include "tinytest.h"

/* A real module (apps/probe) in a real interpreter, with the device
 * replaced by the recorders below. */

#define SURFACE_W 64
#define SURFACE_H 64

/* The rows apps/probe paints, as it names them. */
#define ROW_PRESSES       0
#define ROW_TIMER         2
#define ROW_NOT_DRAWING   4
#define ROW_BAD_ARGUMENTS 6
#define ROW_MEASURES      8
#define ROW_MORE_REFUSALS 10
#define ROW_BITMAP        12
#define ROW_STORE         16
#define ROW_LINE          14
#define ROW_DATA          18
#define ROW_TEXT          20
#define ROW_COLOR         60
#define ROW_SCENE_STATE   61
#define ROW_LOCAL_TIME    62
#define ROW_VALUES        63
#define ROW_PICTURE       44
#define ROW_UI            1

static uint8_t bits[SURFACE_W / 8 * SURFACE_H];
static gfx_t surface;

static char logged[256];
static int  timer_set_id;
static int  timer_set_ms;

/* 0: the device does not know the time, as at start. */
static int64_t fake_now;

static int64_t fake_unix_time(void)
{
    return fake_now;
}

static int32_t fake_time_zone_offset(void)
{
    return 3600;
}

/* A zone an hour east of UTC that moves its clocks another hour ahead at
 * the start of 2030: summer time, as an application meets it. */
#define SUMMER_BEGINS 1893456000 /* 2030-01-01 00:00:00 UTC */
static bool use_summer_zone;
static int32_t fake_time_zone_offset_at(int64_t unix_time)
{
    if (use_summer_zone) {
        time_t moment = (time_t)unix_time;
        struct tm local;
        localtime_r(&moment, &local);
        return (int32_t)local.tm_gmtoff;
    }
    return unix_time >= SUMMER_BEGINS - 3600 ? 7200 : 3600;
}

/* Stands still, so that the probe can check what it reads, unless a test
 * lets it run: then every look at it is a millisecond later. */
#define FAKE_UPTIME_MS 1234
static bool    clock_runs;
static int64_t clock_ms;
static int64_t clock_step_ms = 1;

static int64_t fake_uptime_ms(void)
{
    if (clock_runs) {
        clock_ms += clock_step_ms;
    }
    return clock_ms;
}

/* What one line of the log costs the clock, for apps/budget. */
static int64_t log_cost_ms;

static void record_log(int level, const char *text, int length)
{
    (void)level;
    clock_ms += log_cost_ms;
    size_t used = strlen(logged);
    size_t room = sizeof logged - used - 2; /* the separator and the end */
    if (used + 2 > sizeof logged) {
        return;
    }
    strncat(logged, text, (size_t)length < room ? (size_t)length : room);
    strcat(logged, ";");
}

static void record_timer(int id, int32_t milliseconds)
{
    timer_set_id = id;
    timer_set_ms = milliseconds;
}

/* One application's store, in memory: a few keys, small values. */
#define FAKE_KV_SLOTS 4
static struct {
    char    app_name[16];
    char    key[HOST_KV_KEY_MAX + 1];
    uint8_t value[HOST_KV_VALUE_MAX];
    int32_t length;
} kv_slots[FAKE_KV_SLOTS];

static int kv_slot_of(const char *app_name, const char *key)
{
    for (int slot = 0; slot < FAKE_KV_SLOTS; slot++) {
        if (kv_slots[slot].length > 0 && strcmp(kv_slots[slot].app_name, app_name) == 0 &&
            strcmp(kv_slots[slot].key, key) == 0) {
            return slot;
        }
    }
    return -1;
}

static int32_t fake_kv_get(const char *app_name, const char *key, void *value, int32_t capacity)
{
    int slot = kv_slot_of(app_name, key);
    if (slot < 0) {
        return HOST_ERR_NOT_FOUND;
    }
    if (kv_slots[slot].length > capacity) {
        return HOST_ERR_NO_SPACE;
    }
    memcpy(value, kv_slots[slot].value, (size_t)kv_slots[slot].length);
    return kv_slots[slot].length;
}

static int32_t fake_kv_set(const char *app_name, const char *key, const void *value,
                           int32_t value_length)
{
    int slot = kv_slot_of(app_name, key);
    if (value_length == 0) {
        if (slot >= 0) {
            kv_slots[slot].length = 0;
        }
        return HOST_OK;
    }
    for (int free_slot = 0; slot < 0 && free_slot < FAKE_KV_SLOTS; free_slot++) {
        if (kv_slots[free_slot].length == 0) {
            slot = free_slot;
        }
    }
    if (slot < 0) {
        return HOST_ERR_NO_SPACE;
    }
    strcpy(kv_slots[slot].app_name, app_name);
    strcpy(kv_slots[slot].key, key);
    memcpy(kv_slots[slot].value, value, (size_t)value_length);
    kv_slots[slot].length = value_length;
    return HOST_OK;
}

/* The application has one file, ten bytes long. */
#define FAKE_FILE_NAME    "data.bin"
#define FAKE_FILE_CONTENT "0123456789"

static int32_t fake_file_size(const char *app_name, const char *file)
{
    (void)app_name;
    return strcmp(file, FAKE_FILE_NAME) == 0 ? (int32_t)strlen(FAKE_FILE_CONTENT)
                                             : HOST_ERR_NOT_FOUND;
}

static int32_t fake_file_read(const char *app_name, const char *file, int32_t offset,
                              void *bytes, int32_t capacity)
{
    int32_t size = fake_file_size(app_name, file);
    if (size < 0) {
        return size;
    }
    int32_t left = offset < size ? size - offset : 0;
    int32_t count = left < capacity ? left : capacity;
    memcpy(bytes, &FAKE_FILE_CONTENT[offset], (size_t)count);
    return count;
}

/* The application has polls 0 and 1. */
static int polled_now = -1;
static bool fake_poll_now(const char *app_name, int poll)
{
    (void)app_name;
    if (poll < 0 || poll > 1) {
        return false;
    }
    polled_now = poll;
    return true;
}

static char asked_request[2048]; /* every field at its longest: gcc checks it may be cut */
static int  asked_tag;
static int32_t fake_http_request(const char *app_name, const app_request_t *request)
{
    (void)app_name;
    asked_tag = request->tag;
    snprintf(asked_request, sizeof asked_request, "%d %s|%d:%s=%s,%s=%s|%.*s", request->method,
             request->url, request->header_count,
             request->header_count > 0 ? request->headers[0].name : "",
             request->header_count > 0 ? request->headers[0].value : "",
             request->header_count > 1 ? request->headers[1].name : "",
             request->header_count > 1 ? request->headers[1].value : "",
             (int)request->body_length, request->body_length > 0 ? (const char *)request->body : "");
    return HOST_OK;
}

static uint32_t paced_seconds, alarm_seconds = 99;
static int32_t fake_poll_set_interval(const char *app_name, int poll, uint32_t every_s)
{
    (void)app_name;
    if (poll != 0) {
        return HOST_ERR_ARGUMENT;
    }
    if (every_s == 600) {
        return HOST_ERR_FAILED;
    }
    paced_seconds = every_s;
    return HOST_OK;
}

static int     alarm_number = -1;
static int64_t alarm_time;
static bool fake_alarm_set(const char *app_name, int alarm, int64_t when, bool is_absolute)
{
    (void)app_name;
    if (is_absolute) {
        alarm_time = when;
    } else {
        alarm_number = alarm;
        alarm_seconds = (uint32_t)when;
    }
    return true;
}

static char written_file[32];
static int32_t fake_file_write(const char *app_name, const char *file, int32_t offset,
                               const void *bytes, int32_t length)
{
    (void)app_name;
    snprintf(written_file, sizeof written_file, "%s@%d:%.*s", file, (int)offset, (int)length,
             (const char *)bytes);
    return HOST_OK;
}

#define FAKE_BOOT_ID 0xB007B007u
static uint32_t fake_boot_id(void) { return FAKE_BOOT_ID; }
static bool always_dithers(void) { return true; }
static const app_services_t SERVICES = {
    .unix_time = fake_unix_time,
    .time_zone_offset = fake_time_zone_offset,
    .time_zone_offset_at = fake_time_zone_offset_at,
    .uptime_ms = fake_uptime_ms,
    .boot_id = fake_boot_id,
    .log_write = record_log,
    .timer_set = record_timer,
    .kv_get = fake_kv_get,
    .kv_set = fake_kv_set,
    .dithers = always_dithers,
    .poll_run = fake_poll_now,
    .http_send = fake_http_request,
    .poll_set_interval = fake_poll_set_interval,
    .alarm_set = fake_alarm_set,
    .file_size = fake_file_size,
    .file_read = fake_file_read,
    .file_write = fake_file_write,
};

static const uint8_t *probe_bytes = probe_wasm;
static size_t probe_size = sizeof probe_wasm;

static app_t *load_probe_as(const char *name)
{
    logged[0] = '\0';
    timer_set_id = timer_set_ms = -1;
    clock_runs = false;
    clock_ms = FAKE_UPTIME_MS;
    app_t *app = NULL;
    CHECK(app_load(name, HOST_API_VERSION, probe_bytes, probe_size, APP_GRANT_FILES, &SERVICES, &app) == APP_OK);
    return app;
}

static app_t *load_probe(void)
{
    return load_probe_as("probe");
}

static void draw(app_t *app)
{
    gfx_init(&surface, bits, SURFACE_W, SURFACE_H);
    gfx_clear(&surface, false);
    app_draw(app, &surface);
}

static int black_in_row(int row)
{
    int count = 0;
    for (int x = 0; x < SURFACE_W; x++) {
        count += gfx_get(&surface, x, row);
    }
    return count;
}

static void test_lifecycle(void)
{
    app_t *app = load_probe();
    CHECK_STR(logged, "init;");

    app_focus(app);
    CHECK(timer_set_id == 1 && timer_set_ms == 500);

    app_blur(app);
    CHECK_STR(logged, "init;blur;");
    CHECK(!app_has_stopped(app));
    app_unload(app);
}

static void test_scene_state(void)
{
    app_session_t session = {0};
    app_t *app = load_probe();
    app_attach_session(app, &session, true);
    app_button(app, 13, BTN_PRESSED);
    app_blur(app);
    CHECK(session.length == HOST_SESSION_MAX);
    app_unload(app);

    app = load_probe();
    app_attach_session(app, &session, true);
    app_button(app, 14, BTN_PRESSED);
    draw(app);
    CHECK(gfx_get(&surface, 0, ROW_SCENE_STATE));
    CHECK(gfx_get(&surface, 2, ROW_SCENE_STATE));
    CHECK(gfx_get(&surface, 4, ROW_SCENE_STATE));
    app_button(app, 15, BTN_PRESSED);
    draw(app);
    CHECK(gfx_get(&surface, 6, ROW_SCENE_STATE));
    CHECK(gfx_get(&surface, 8, ROW_SCENE_STATE));
    CHECK(session.length == 0 && session.bytes == NULL);
    app_unload(app);
}

static void test_buttons_and_redraw(void)
{
    app_t *app = load_probe();
    CHECK(!app_take_redraw_request(app));

    app_button(app, 2, BTN_PRESSED);
    CHECK(app_take_redraw_request(app));
    CHECK(!app_take_redraw_request(app)); /* asked once, told once */

    app_button(app, 1, BTN_PRESSED);
    draw(app);
    CHECK(black_in_row(ROW_PRESSES) == 3); /* 2 + 1: the state lives in the module */
    for (int action = 0; action < 16; action++) {
        app_button(app, BTN_OK, action);
        CHECK(!app_has_stopped(app));
    }
    draw(app);
    CHECK(black_in_row(ROW_PRESSES) == 35);
    app_unload(app);
}

static void test_exit(void)
{
    app_t *app = load_probe();
    CHECK(!app_take_exit_request(app));
    app_button(app, 7, BTN_PRESSED);
    CHECK(app_take_exit_request(app));
    CHECK(!app_take_exit_request(app)); /* asked once, told once */
    app_unload(app);
}

static void test_timer(void)
{
    app_t *app = load_probe();
    draw(app);
    CHECK(black_in_row(ROW_TIMER) == 0);
    app_timer(app, 2);
    CHECK(app_take_redraw_request(app));
    draw(app);
    CHECK(black_in_row(ROW_TIMER) == 3);
    app_unload(app);
}

static void test_drawing(void)
{
    app_t *app = load_probe();
    draw(app);
    CHECK(gfx_get(&surface, 0, ROW_BITMAP) && !gfx_get(&surface, 1, ROW_BITMAP) &&
          gfx_get(&surface, 2, ROW_BITMAP));
    CHECK(black_in_row(ROW_LINE) == 4);
    /* y is the top of the line: the glyph is under it, not above */
    CHECK(black_in_row(ROW_TEXT - 1) == 0);
    int text_ink = 0;
    for (int row = ROW_TEXT; row < ROW_TEXT + gfx_font_height(gfx_font_system()); row++) {
        text_ink += black_in_row(row);
    }
    CHECK(text_ink > 0);
    app_unload(app);
}

static void test_refusals(void)
{
    app_t *app = load_probe();
    /* a wild pointer and an unknown font come back as errors, not crashes */
    draw(app);
    CHECK(gfx_get(&surface, 0, ROW_BAD_ARGUMENTS) && gfx_get(&surface, 2, ROW_BAD_ARGUMENTS));
    CHECK(black_in_row(ROW_NOT_DRAWING) == 0);

    /* drawing from a button handler is refused and paints nothing */
    app_button(app, 3, BTN_PRESSED);
    CHECK(!app_take_redraw_request(app));
    draw(app);
    CHECK(black_in_row(ROW_NOT_DRAWING) == 1);
    CHECK(black_in_row(ROW_PRESSES) == 0);
    app_unload(app);
}

static void test_measures_and_time(void)
{
    app_t *app = load_probe();
    draw(app);
    /* text width adds up, a font has a height, both clocks answer, the
     * three fonts are three sizes */
    CHECK(black_in_row(ROW_MEASURES) == 5);
    app_unload(app);
}

static void test_more_refusals(void)
{
    app_t *app = load_probe();
    draw(app);
    /* a timer that does not exist, a negative delay, bits outside the
     * memory, a negative length, a rectangle and a line of two thousand
     * million pixels: six refusals; and text too long is cut to the limit */
    CHECK(black_in_row(ROW_MORE_REFUSALS) == 7);
    /* none of the refused timers reached the device */
    CHECK(timer_set_id == -1);
    app_unload(app);
}

static void test_store(void)
{
    memset(kv_slots, 0, sizeof kv_slots);
    app_t *app = load_probe();
    draw(app);
    /* nothing stored yet; the value written is read back whole; a buffer
     * too small is refused; a removed key is gone; keys that are not keys
     * and a value too large are refused */
    CHECK(black_in_row(ROW_STORE) == 7);
    app_unload(app);

    /* what one life of the application left, the next one finds... */
    app = load_probe();
    draw(app);
    CHECK(gfx_get(&surface, 20, ROW_STORE));
    app_unload(app);

    /* ...and another application does not: each has a store of its own */
    app = load_probe_as("other");
    draw(app);
    CHECK(!gfx_get(&surface, 20, ROW_STORE));
    app_unload(app);
}

static void test_response_headers(void)
{
    app_t *app = load_probe();
    answer_headers_t headers;
    answer_headers_reset(&headers);
    answer_headers_add(&headers, "Set-Cookie", "sid=one");
    answer_headers_add(&headers, "Set-Cookie", "sid=two");
    answer_headers_add(&headers, "X-NDM-Realm", "router");
    answer_headers_add(&headers, "empty", "");
    app_answer_begins(app, NULL, &headers, 401, -1);
    app_data(app, 30, NULL, 0, HOST_DATA_FIRST | HOST_DATA_LAST, 401);
    app_answer_ends(app);
    CHECK(app_take_focus_request(app));
    app_answer_begins(app, NULL, &headers, 401, -1);
    app_values(app, 30, 401, NULL);
    app_answer_ends(app);
    CHECK(app_take_focus_request(app));
    app_data(app, 30, NULL, 0, HOST_DATA_FIRST | HOST_DATA_LAST, 200);
    CHECK(!app_take_focus_request(app));

    char too_large[ANSWER_HEADERS_BYTES + 1];
    memset(too_large, 'x', sizeof too_large - 1);
    too_large[sizeof too_large - 1] = 0;
    answer_headers_add(&headers, "overflow", too_large);
    host_answer_info_t metadata = { .etag = "kept" };
    app_answer_begins(app, &metadata, &headers, 200, -1);
    app_data(app, 34, NULL, 0, HOST_DATA_FIRST | HOST_DATA_LAST, 200);
    app_answer_ends(app);
    CHECK(app_take_focus_request(app));
    app_unload(app);
    app = load_probe_as("other");
    app_answer_begins(app, NULL, NULL, 200, -1);
    app_data(app, 33, NULL, 0, HOST_DATA_FIRST | HOST_DATA_LAST, 200);
    app_answer_ends(app);
    CHECK(app_take_focus_request(app));
    app_unload(app);
}

static void test_data(void)
{
    app_t *app = load_probe();
    static uint8_t response[2500];
    memset(response, 3, sizeof response);

    /* a response of two and a half kilobytes, the way the host cuts it */
    CHECK(!app_take_focus_request(app));
    app_data(app, 1, response, HOST_DATA_CHUNK_MAX, HOST_DATA_FIRST, 200);
    app_data(app, 1, response + 1024, HOST_DATA_CHUNK_MAX, 0, 0);
    CHECK(!app_take_focus_request(app));
    app_data(app, 1, response + 2048, 2500 - 2048, HOST_DATA_LAST, 0);
    /* the probe asks for the screen once it has all of it */
    CHECK(app_take_focus_request(app));
    CHECK(!app_take_focus_request(app));

    /* a request that could not be made */
    app_data(app, 2, NULL, 0, HOST_DATA_FAILED, 0);

    draw(app);
    CHECK(gfx_get(&surface, 0, ROW_DATA) && gfx_get(&surface, 2, ROW_DATA));
    CHECK(gfx_get(&surface, 0, ROW_UI));

    /* what the headers said is there for the handler to ask, and only then */
    host_answer_info_t described = { .length = -1, .retry_after = 30, .etag = "\"v7\"" };
    app_answer_begins(app, &described, NULL, 304, -1);
    app_data(app, 3, response, 0, HOST_DATA_FIRST | HOST_DATA_LAST, 304);
    app_answer_ends(app);
    CHECK(app_take_focus_request(app));
    app_data(app, 3, response, 0, HOST_DATA_FIRST | HOST_DATA_LAST, 304);
    CHECK(!app_take_focus_request(app));

    /* JSON that comes in pieces is picked apart by the application's library */
    static const char JSON[] = "{\"count\":2,\"items\":[{\"name\":\"first\"},{\"name\":\"second\"}]}";
    size_t half = sizeof JSON / 2;
    app_data(app, 4, (const uint8_t *)JSON, half, HOST_DATA_FIRST, 200);
    app_data(app, 4, (const uint8_t *)JSON + half, sizeof JSON - 1 - half, HOST_DATA_LAST, 0);
    CHECK(app_take_focus_request(app));

    /* a piece larger than the buffer is not delivered, let alone written */
    static uint8_t too_large[HOST_DATA_CHUNK_MAX + 1];
    app_data(app, 1, too_large, sizeof too_large, HOST_DATA_FIRST | HOST_DATA_LAST, 200);
    CHECK(!app_take_focus_request(app));
    CHECK(!app_has_stopped(app));
    app_unload(app);
}

static void test_trap_stops_the_application(void)
{
    app_t *app = load_probe();
    app_button(app, 4, BTN_PRESSED);
    CHECK(app_has_stopped(app));
    CHECK(app_stop_reason(app)[0] != '\0');
    CHECK(app_take_redraw_request(app)); /* the screen has to say so */

    /* nothing runs in it any more */
    logged[0] = '\0';
    app_blur(app);
    CHECK_STR(logged, "");
    draw(app);
    CHECK(black_in_row(ROW_LINE) == 0);
    app_unload(app);
}

static void test_runaway_handlers_are_stopped(void)
{
    /* a loop that calls nothing and never ends */
    app_t *app = load_probe();
    clock_runs = true;
    app_button(app, 8, BTN_PRESSED);
    CHECK(app_has_stopped(app));
    CHECK_STR(app_stop_reason(app), "it ran too long");
    app_unload(app);

    /* recursion without an end: stopped before it runs through the stack,
     * and by the stack, not by the clock */
    app = load_probe();
    clock_runs = true;
    app_button(app, 9, BTN_PRESSED);
    CHECK(app_has_stopped(app));
    CHECK(strcmp(app_stop_reason(app), "it ran too long") != 0);
    app_unload(app);

    /* and an application that behaves is not cut short by a slow clock */
    app = load_probe();
    clock_runs = true;
    app_button(app, 1, BTN_PRESSED);
    draw(app);
    CHECK(!app_has_stopped(app));
    app_unload(app);
}

static void test_name_subsection_bounds(void)
{
    /* An empty import section must not stop validation of later custom
     * sections. The length wraps back six bytes on a 32-bit device. */
    static const uint8_t wrapping_name[] = {
        0x00, 'a', 's', 'm', 0x01, 0x00, 0x00, 0x00,
        0x02, 0x01, 0x00,
        0x00, 0x0b, 0x04, 'n', 'a', 'm', 'e', 0x00, 0xfa, 0xff, 0xff, 0xff, 0x0f
    };
    static const uint8_t overlong_name[] = {
        0x00, 'a', 's', 'm', 0x01, 0x00, 0x00, 0x00,
        0x00, 0x07, 0x04, 'n', 'a', 'm', 'e', 0x00, 0x01
    };
    static const uint8_t truncated_length[] = {
        0x00, 'a', 's', 'm', 0x01, 0x00, 0x00, 0x00,
        0x00, 0x07, 0x04, 'n', 'a', 'm', 'e', 0x00, 0x80
    };
    static const uint8_t nul_name[] = {
        0x00, 'a', 's', 'm', 0x01, 0x00, 0x00, 0x00,
        0x00, 0x0d, 0x06, 'n', 'a', 'm', 'e', 0x00, 'x', 0x00, 0xfa, 0xff, 0xff, 0xff, 0x0f
    };
    const struct { const uint8_t *bytes; size_t size; } invalid[] = {
        { wrapping_name, sizeof wrapping_name }, { overlong_name, sizeof overlong_name },
        { truncated_length, sizeof truncated_length }, { nul_name, sizeof nul_name },
    };
    for (size_t index = 0; index < sizeof invalid / sizeof invalid[0]; index++) {
        app_t *app = (app_t *)1;
        CHECK(app_load("x", HOST_API_VERSION, invalid[index].bytes, invalid[index].size,
                       0, &SERVICES, &app) == APP_ERR_BAD_MODULE);
        CHECK(app == NULL);
        app_unload(app);
    }
    /* Empty and exactly bounded subsections remain valid, including an
     * unknown subsection type, which wasm3 skips. */
    static const uint8_t valid_name[] = {
        0x00, 'a', 's', 'm', 0x01, 0x00, 0x00, 0x00,
        0x02, 0x01, 0x00,
        0x00, 0x0c, 0x04, 'n', 'a', 'm', 'e', 0x00, 0x00, 0x01, 0x01, 0x00, 0x7f, 0x00
    };
    app_t *app = NULL;
    CHECK(app_load("x", HOST_API_VERSION, valid_name, sizeof valid_name, 0, &SERVICES, &app) == APP_OK);
    app_unload(app);
    app = load_probe();
    CHECK(!app_has_stopped(app));
    app_unload(app);
}

static void test_bad_modules(void)
{
    app_t *app = NULL;
    static const uint8_t garbage[] = { 'n', 'o', 't', ' ', 'w', 'a', 's', 'm' };
    CHECK(app_load("x", 2, garbage, sizeof garbage, 0, &SERVICES, &app) == APP_ERR_OTHER_API);
    CHECK(app == NULL);
    CHECK(app_load("x", 4, garbage, sizeof garbage, 0, &SERVICES, &app) == APP_ERR_OTHER_API);
    CHECK(app_load("x", HOST_API_VERSION, garbage, sizeof garbage, 0, &SERVICES, &app) == APP_ERR_BAD_MODULE);

    /* an export the host calls, declared otherwise than the contract has it: not run at all */
    app = (app_t *)1;
    CHECK(app_load("x", HOST_API_VERSION, wrongret_wasm, sizeof wrongret_wasm, 0, &SERVICES, &app) ==
          APP_ERR_BAD_MODULE && app == NULL);
    app = (app_t *)1;
    CHECK(app_load("x", HOST_API_VERSION, wrongarg_wasm, sizeof wrongarg_wasm, 0, &SERVICES, &app) ==
          APP_ERR_BAD_MODULE && app == NULL);

    /* a valid header and then nothing sensible */
    static const uint8_t truncated[] = { 0x00, 'a', 's', 'm', 0x01, 0x00, 0x00, 0x00, 0x05, 0x7F };
    CHECK(app_load("x", HOST_API_VERSION, truncated, sizeof truncated, 0, &SERVICES, &app) == APP_ERR_BAD_MODULE);

    /* (module (memory 2)): more than the one page an application gets */
    static const uint8_t two_pages[] = { 0x00, 'a', 's', 'm', 0x01, 0x00, 0x00, 0x00,
                                         0x05, 0x03, 0x01, 0x00, 0x02 };
    CHECK(app_load("x", HOST_API_VERSION, two_pages, sizeof two_pages, 0, &SERVICES, &app) == APP_ERR_BAD_MODULE);
    CHECK(app == NULL);

    /* Declared or unbounded growth is refused even when the initial size fits. */
    static const uint8_t growable[][16] = {
        { 0x00, 'a', 's', 'm', 1, 0, 0, 0, 5, 4, 1, 1, 0, 1 },
        { 0x00, 'a', 's', 'm', 1, 0, 0, 0, 5, 3, 1, 0, 0 },
        { 0x00, 'a', 's', 'm', 1, 0, 0, 0, 5, 5, 1, 9, 1, 2, 0 },
        { 0x00, 'a', 's', 'm', 1, 0, 0, 0, 5, 4, 1, 8, 1, 0 },
    };
    for (size_t index = 0; index < sizeof growable / sizeof growable[0]; index++) {
        app = (app_t *)1;
        size_t size = 8 + 2 + growable[index][9];
        CHECK(app_load("growable", HOST_API_VERSION, growable[index], size, 0, &SERVICES, &app) ==
              APP_ERR_BAD_MODULE && app == NULL);
    }

    /* A memory of 65537 one-byte pages, initial and maximum: a byte more than an application gets */
    static const uint8_t byte_over[] = { 0x00, 'a', 's', 'm', 0x01, 0x00, 0x00, 0x00,
                                         0x05, 0x09, 0x01, 0x09, 0x81, 0x80, 0x04, 0x81, 0x80, 0x04, 0x00 };
    app = (app_t *)1;
    CHECK(app_load("x", HOST_API_VERSION, byte_over, sizeof byte_over, 0, &SERVICES, &app) == APP_ERR_BAD_MODULE);
    CHECK(app == NULL);

    /* (module (type (func)) (import "host" "evil" (func (type 0)))) */
    static const uint8_t unknown_import[] = { 0x00, 'a',  's',  'm',  0x01, 0x00, 0x00, 0x00,
                                              0x01, 0x04, 0x01, 0x60, 0x00, 0x00, 0x02, 0x0D,
                                              0x01, 0x04, 'h',  'o',  's',  't',  0x04, 'e',
                                              'v',  'i',  'l',  0x00, 0x00 };
    /* a call the host does not have: a module made for another version of it */
    CHECK(app_load("x", HOST_API_VERSION, unknown_import, sizeof unknown_import, 0, &SERVICES, &app) ==
          APP_ERR_OTHER_API);

    /* (module (type (func (param i32 i32 i32) (result i32)))
     *         (import "host" "sys_info" (func (type 0))))
     * the system API is for an application that asked for it */
    static const uint8_t system_import[] = { 0x00, 'a',  's',  'm',  0x01, 0x00, 0x00, 0x00,
                                             0x01, 0x08, 0x01, 0x60, 0x03, 0x7F, 0x7F, 0x7F,
                                             0x01, 0x7F, 0x02, 0x11, 0x01, 0x04, 'h',  'o',
                                             's',  't',  0x08, 's',  'y',  's',  '_',  'i',
                                             'n',  'f',  'o',  0x00, 0x00 };
    CHECK(app_load("x", HOST_API_VERSION, system_import, sizeof system_import, 0, &SERVICES, &app) == APP_ERR_BAD_MODULE);
    CHECK(app_load("x", HOST_API_VERSION, system_import, sizeof system_import, APP_GRANT_FILES, &SERVICES, &app) ==
          APP_ERR_BAD_MODULE);
    CHECK(app_load("x", HOST_API_VERSION, system_import, sizeof system_import, APP_GRANT_SYSTEM, &SERVICES, &app) == APP_OK);
    app_unload(app);

    /* (module (type (func (param i32 i32 i32 i32 i32) (result i32)))
     *         (import "host" "file_write" (func (type 0))))
     * writing files is for an application that said it writes */
    static const uint8_t file_write_import[] = { 0x00, 'a', 's', 'm', 0x01, 0x00, 0x00, 0x00,
                                                 0x01, 0x0A, 0x01, 0x60, 0x05, 0x7F, 0x7F, 0x7F,
                                                 0x7F, 0x7F, 0x01, 0x7F, 0x02, 0x13, 0x01, 0x04,
                                                 'h',  'o',  's',  't',  0x0A, 'f',  'i',  'l',
                                                 'e',  '_',  'w',  'r',  'i',  't',  'e',  0x00,
                                                 0x00 };
    CHECK(app_load("x", HOST_API_VERSION, file_write_import, sizeof file_write_import, APP_GRANT_SYSTEM, &SERVICES,
                   &app) == APP_ERR_BAD_MODULE);
    CHECK(app_load("x", HOST_API_VERSION, file_write_import, sizeof file_write_import, APP_GRANT_FILES, &SERVICES,
                   &app) == APP_OK);
    app_unload(app);

    /* A module that imports every call of host_api.h, each as the header
     * declares it: the host's table has them all, with those signatures. */
    CHECK(app_load("x", HOST_API_VERSION, every_call_wasm, sizeof every_call_wasm, APP_GRANT_SYSTEM | APP_GRANT_FILES, &SERVICES, &app) == APP_OK);

    /* One instance at a time: a second is refused while the first is loaded,
     * and the first keeps running. */
    app_t *second = (app_t *)1;
    CHECK(app_load("y", HOST_API_VERSION, every_call_wasm, sizeof every_call_wasm, APP_GRANT_SYSTEM | APP_GRANT_FILES, &SERVICES, &second) ==
          APP_ERR_NO_MEMORY && second == NULL);
    app_unload(app);
    CHECK(app_load("y", HOST_API_VERSION, every_call_wasm, sizeof every_call_wasm, APP_GRANT_SYSTEM | APP_GRANT_FILES, &SERVICES, &second) == APP_OK);
    app_unload(second);

    /* a name the host has, asked of another module */
    static const uint8_t other_module[] = { 0x00, 'a',  's',  'm',  0x01, 0x00, 0x00, 0x00,
                                            0x01, 0x04, 0x01, 0x60, 0x00, 0x00, 0x02, 0x12,
                                            0x01, 0x03, 'e',  'n',  'v',  0x0A, 'g',  'f',
                                            'x',  '_',  'h',  'e',  'i',  'g',  'h',  't',
                                            0x00, 0x00 };
    CHECK(app_load("x", HOST_API_VERSION, other_module, sizeof other_module, 0, &SERVICES, &app) == APP_ERR_BAD_MODULE);

    /* an import section that says it is longer than the file */
    static const uint8_t lying_section[] = { 0x00, 'a', 's', 'm', 0x01, 0x00, 0x00, 0x00,
                                             0x02, 0x7F, 0x01 };
    CHECK(app_load("x", HOST_API_VERSION, lying_section, sizeof lying_section, 0, &SERVICES, &app) == APP_ERR_BAD_MODULE);

    app_unload(NULL); /* allowed */
}

/* What a poll's "pick" took out of an answer: a number, a text, and a path
 * the answer does not have. */
static json_pick_t *picked_values(void)
{
    static const char *const PATHS[] = { "t", "w", "none", "l[*]" };
    static const char        ANSWER[] = "{\"t\":-3.46,\"w\":\"Rain\",\"l\":[\"a\",\"b\"]}";
    static json_pick_t       picked;
    static char              list[64];
    json_pick_init(&picked, PATHS, 4);
    json_pick_lists(&picked, list, sizeof list);
    json_pick_feed(&picked, ANSWER, sizeof ANSWER - 1);
    json_pick_finish(&picked);
    return &picked;
}

static void test_values(void)
{
    app_t *app = load_probe();
    app_values(app, 3, 200, picked_values());
    CHECK(app_take_redraw_request(app));
    app_values(app, 5, 0, NULL); /* the request could not be made */
    draw(app);
    /* read right, the failure told apart, nothing to read outside the handler,
     * and the SDK's numbers */
    CHECK(gfx_get(&surface, 0, ROW_VALUES) && gfx_get(&surface, 2, ROW_VALUES));
    CHECK(gfx_get(&surface, 4, ROW_VALUES) && gfx_get(&surface, 6, ROW_VALUES));
    /* and a call through a function pointer, which the trimmed interpreter still has */
    CHECK(gfx_get(&surface, 8, ROW_VALUES));
    /* the arena hands the free part of the page out and takes it back */
    CHECK(gfx_get(&surface, 20, ROW_VALUES));
    /* the network: online, a poll asked for by its number, a wrong number refused */
    CHECK(gfx_get(&surface, 12, ROW_VALUES) && polled_now == 1);
    /* a request of its own: the method, the URL, the headers taken apart, the body */
    if (probe_bytes == probe_wasm) {
        CHECK(gfx_get(&surface, 18, ROW_VALUES));
        CHECK_STR(asked_request,
                  "1 https://api.example.com/do?x={{token}}|2:Accept=application/json,X-Token={{token}}|{\"a\":1}");
        CHECK(asked_tag == HOST_REQUEST_LAST);
    } else {
        /* The pre-release probe expected tag 256 to fail; it is now accepted. */
        CHECK(!gfx_get(&surface, 18, ROW_VALUES));
        CHECK(asked_tag == 256);
    }
    /* a poll at another pace, an alarm, a file written, and the wrong ones refused */
    CHECK(gfx_get(&surface, 14, ROW_VALUES));
    CHECK(paced_seconds == 3600 && alarm_seconds == 0 && alarm_number == 2);
    CHECK(alarm_time == 1800000000);
    CHECK_STR(written_file, "cache.bin@4:abc");
    /* the alarm comes back as a call */
    app_alarm(app, 2);
    draw(app);
    CHECK(gfx_get(&surface, 16, ROW_VALUES));
    /* its file: the length, a piece from the middle, the end, the names it may not use */
    CHECK(gfx_get(&surface, 10, ROW_VALUES));
    /* a picture straight from the file: '0' and '1' are 00110000 00110001 */
    CHECK(!gfx_get(&surface, 1, ROW_PICTURE) && gfx_get(&surface, 2, ROW_PICTURE) &&
          gfx_get(&surface, 3, ROW_PICTURE) && !gfx_get(&surface, 4, ROW_PICTURE));
    CHECK(gfx_get(&surface, 10, ROW_PICTURE) && gfx_get(&surface, 11, ROW_PICTURE) &&
          !gfx_get(&surface, 14, ROW_PICTURE) && gfx_get(&surface, 15, ROW_PICTURE));
    /* the host's plus, a cell one pixel: its bar and its post */
    CHECK(gfx_get(&surface, 2, ROW_PICTURE + 2 + 5) && gfx_get(&surface, 8, ROW_PICTURE + 2 + 5) &&
          !gfx_get(&surface, 9, ROW_PICTURE + 2 + 5));
    CHECK(gfx_get(&surface, 5, ROW_PICTURE + 2 + 2) && !gfx_get(&surface, 4, ROW_PICTURE + 2 + 2));
    /* a file that is not there, one too short, an icon there is none of */
    CHECK(gfx_get(&surface, 30, ROW_PICTURE));
    CHECK(!app_has_stopped(app));
    app_unload(app);
}

static void test_screen_inversion(void)
{
    app_session_t session = {0};
    app_t *app = load_probe();
    CHECK(!app_screen_inverted(app));
    app_attach_session(app, &session, true);
    app_button(app, 100, BTN_PRESSED);
    CHECK(app_screen_inverted(app) && session.screen_inverted);
    CHECK(app_take_redraw_request(app));
    app_button(app, 100, BTN_PRESSED);
    CHECK(!app_take_redraw_request(app));
    app_unload(app);
    app = load_probe();
    app_attach_session(app, &session, false);
    CHECK(app_screen_inverted(app));
    app_button(app, 101, BTN_PRESSED);
    CHECK(!app_screen_inverted(app) && session.screen_inverted);
    app_unload(app);
    app = load_probe();
    app_attach_session(app, &session, true);
    CHECK(app_screen_inverted(app));
    app_button(app, 101, BTN_PRESSED);
    CHECK(!app_screen_inverted(app) && !session.screen_inverted);
    session.is_closed = true;
    app_button(app, 100, BTN_PRESSED);
    CHECK(!session.screen_inverted);
    app_unload(app);
    app = load_probe_as("another");
    CHECK(!app_screen_inverted(app));
    app_unload(app);
}

static void test_redraw_flags(void)
{
    app_t *app = load_probe();
    app_button(app, 2, BTN_PRESSED);
    CHECK(app_take_redraw_request(app) && app_take_redraw_flags(app) == HOST_REDRAW_NORMAL);

    app_button(app, 10, BTN_PRESSED);
    CHECK(app_take_redraw_request(app) && app_take_redraw_flags(app) == HOST_REDRAW_SOON);
    CHECK(app_take_redraw_flags(app) == 0); /* asked once, told once */

    /* two requests in a row: both wishes stand */
    app_button(app, 10, BTN_PRESSED);
    app_button(app, 11, BTN_PRESSED);
    CHECK(app_take_redraw_flags(app) == (HOST_REDRAW_SOON | HOST_REDRAW_CLEAN));
    app_button(app, 12, BTN_PRESSED);
    CHECK(app_take_redraw_flags(app) == HOST_REDRAW_NO_BLINK);
    app_unload(app);
}

static void test_color_and_local_time(void)
{
    app_t *app = load_probe();
    draw(app);
    /* a bar of eight with a hole of two, the mark for a refused colour, and
     * half of a bar of sixteen drawn in a grey */
    CHECK(black_in_row(ROW_COLOR) == 7 + 8);
    CHECK(gfx_get(&surface, 16, ROW_COLOR) && !gfx_get(&surface, 17, ROW_COLOR));
    CHECK(!gfx_get(&surface, 2, ROW_COLOR) && !gfx_get(&surface, 3, ROW_COLOR));
    /* every draw starts in ink again */
    draw(app);
    CHECK(gfx_get(&surface, 0, ROW_COLOR));

    CHECK(gfx_get(&surface, 0, ROW_LOCAL_TIME) && !gfx_get(&surface, 2, ROW_LOCAL_TIME));
    CHECK(gfx_get(&surface, 4, ROW_LOCAL_TIME));
    /* any moment taken apart, put together again, across a change of the clocks, past a month's end */
    CHECK(gfx_get(&surface, 6, ROW_LOCAL_TIME) && gfx_get(&surface, 8, ROW_LOCAL_TIME));
    CHECK(gfx_get(&surface, 10, ROW_LOCAL_TIME) && gfx_get(&surface, 12, ROW_LOCAL_TIME));
    CHECK(gfx_get(&surface, 18, ROW_LOCAL_TIME) && gfx_get(&surface, 20, ROW_LOCAL_TIME));
    CHECK(gfx_get(&surface, 22, ROW_LOCAL_TIME)); /* invalid moments leave destinations intact */
    CHECK(gfx_get(&surface, 24, ROW_LOCAL_TIME)); /* extreme normalized fields are refused */
    CHECK(gfx_get(&surface, 26, ROW_LOCAL_TIME)); /* bounds, historical dates and zero round-trip */
    if (probe_bytes == probe_wasm) {
        CHECK(gfx_get(&surface, 28, ROW_LOCAL_TIME)); /* explicit Unix epoch formats */
        CHECK(gfx_get(&surface, 30, ROW_LOCAL_TIME)); /* zero denotes 1970, not unknown */
        CHECK(gfx_get(&surface, 32, ROW_LOCAL_TIME)); /* zero capacity validates without writing */
    }
    const char *previous_zone = getenv("TZ");
    char *kept_zone = previous_zone != NULL ? strdup(previous_zone) : NULL;
    CHECK(setenv("TZ", "CET-1CEST,M3.5.0/2,M10.5.0/3", 1) == 0);
    tzset();
    use_summer_zone = true;
    draw(app);
    CHECK(gfx_get(&surface, 14, ROW_LOCAL_TIME)); /* gap: 02:30 becomes 03:30 */
    CHECK(gfx_get(&surface, 16, ROW_LOCAL_TIME)); /* fold: earlier 02:30 */
    use_summer_zone = false;
    if (kept_zone != NULL) {
        CHECK(setenv("TZ", kept_zone, 1) == 0);
        free(kept_zone);
    } else {
        CHECK(unsetenv("TZ") == 0);
    }
    tzset();
    fake_now = 1794439815; /* 2026-11-11 23:30:15 UTC */
    draw(app);
    CHECK(!gfx_get(&surface, 0, ROW_LOCAL_TIME) && gfx_get(&surface, 2, ROW_LOCAL_TIME));
    fake_now = 0;
    app_unload(app);
}

static void test_probe_contract(void)
{
    test_lifecycle();
    test_buttons_and_redraw();
    test_exit();
    test_timer();
    test_redraw_flags();
    test_drawing();
    test_color_and_local_time();
    test_refusals();
    test_measures_and_time();
    test_more_refusals();
    test_store();
    test_data();
    test_values();
    test_trap_stops_the_application();
    test_runaway_handlers_are_stopped();
    test_bad_modules();
    test_name_subsection_bounds();
}

static void test_manifest_geometry_in_load(void)
{
    app_t *loaded = load_probe();
    draw(loaded);
    CHECK(gfx_get(&surface, 25, 5) && gfx_get(&surface, 29, 5) && gfx_get(&surface, 31, 5));
    app_unload(loaded);
    CHECK(app_load("fullscreen", HOST_API_VERSION, probe_bytes, probe_size,
                   APP_GRANT_FILES | APP_GRANT_FULLSCREEN, &SERVICES, &loaded) == APP_OK);
    draw(loaded);
    CHECK(gfx_get(&surface, 25, 5) && gfx_get(&surface, 27, 5));
    app_unload(loaded);
}

static void test_byte_page_memory(void)
{
    static const uint8_t at_limit[] = {
        0x00, 'a', 's', 'm', 0x01, 0x00, 0x00, 0x00,
        0x05, 0x09, 0x01, 0x09, 0x80, 0x80, 0x04, 0x80, 0x80, 0x04, 0x00
    };
    app_t *app = NULL;
    CHECK(app_load("limit", HOST_API_VERSION, at_limit, sizeof at_limit, 0, &SERVICES, &app) == APP_OK);
    CHECK(app != NULL && !app_has_stopped(app));
    app_unload(app);

    app = NULL;
    CHECK(app_load("memory", HOST_API_VERSION, memory_wasm, sizeof memory_wasm, 0, &SERVICES, &app) == APP_OK);
    CHECK(app != NULL && !app_has_stopped(app));
    CHECK(app_take_redraw_request(app));
    app_unload(app);

    /* (memory 0 0); app_on_load traps if memory.grow(1) succeeds. */
    static const uint8_t memory_zero_wasm[] = {
        0x00, 'a', 's', 'm', 1, 0, 0, 0,
        1, 4, 1, 0x60, 0, 0,
        3, 2, 1, 0,
        5, 4, 1, 1, 0, 0,
        7, 15, 1, 11, 'a', 'p', 'p', '_', 'o', 'n', '_', 'l', 'o', 'a', 'd', 0, 0,
        10, 15, 1, 13, 0, 0x41, 1, 0x40, 0, 0x41, 0x7f, 0x47, 4, 0x40, 0, 0x0b, 0x0b
    };
    app = NULL;
    CHECK(app_load("zero", HOST_API_VERSION, memory_zero_wasm, sizeof memory_zero_wasm, 0, &SERVICES, &app) == APP_OK);
    CHECK(app != NULL && !app_has_stopped(app));
    app_unload(app);
}

static void test_frozen_hello_state(void)
{
    FILE *fixture = fopen("../../../sdk/abi/3/hello/presses.bin", "rb");
    uint8_t original[4];
    CHECK(fixture != NULL && fread(original, 1, sizeof original, fixture) == sizeof original);
    fclose(fixture);
    CHECK(fake_kv_set("hello", "presses", original, sizeof original) == HOST_OK);
    FILE *module = fopen("../../../sdk/abi/3/hello/app.wasm", "rb");
    CHECK(module != NULL);
    CHECK(fseek(module, 0, SEEK_END) == 0);
    long size = ftell(module);
    rewind(module);
    uint8_t *bytes = malloc((size_t)size);
    CHECK(bytes != NULL && fread(bytes, 1, (size_t)size, module) == (size_t)size);
    fclose(module);
    app_t *app = NULL;
    CHECK(app_load("hello", 3, bytes, (size_t)size, 0, &SERVICES, &app) == APP_OK);
    draw(app);
    CHECK(!app_has_stopped(app));
    /* This prerelease binary retains its stored state; button events require a rebuild. */
    app_terminate(app);
    CHECK(!app_has_stopped(app));
    uint8_t changed[4];
    CHECK(fake_kv_get("hello", "presses", changed, sizeof changed) == sizeof changed);
    CHECK(changed[0] == 42 && changed[1] == 0 && changed[2] == 0 && changed[3] == 0);
    app_unload(app);
    free(bytes);
}

static const char *russian(void)
{
    return "ru";
}

static int count_of(const char *text, const char *part)
{
    int count = 0;
    for (const char *at = strstr(text, part); at != NULL; at = strstr(at + 1, part)) {
        count++;
    }
    return count;
}

static void test_host_calls_count_against_the_budget(void)
{
    memset(kv_slots, 0, sizeof kv_slots);
    app_t *app = NULL;
    logged[0] = '\0';
    clock_runs = false;
    clock_ms = FAKE_UPTIME_MS;
    CHECK(app_load("budget", HOST_API_VERSION, budget_wasm, sizeof budget_wasm, 0, &SERVICES, &app) ==
          APP_OK);
    CHECK(!app_has_stopped(app));
    /* a hundred calls of 100 ms each and no loop of a thousand turns: the
     * one that passes 3 s is the last */
    logged[0] = '\0';
    log_cost_ms = 100;
    app_button(app, 1, BTN_PRESSED);
    log_cost_ms = 0;
    CHECK(app_has_stopped(app));
    CHECK_STR(app_stop_reason(app), "it ran too long");
    CHECK(count_of(logged, "x;") == 31);
    app_unload(app);

    /* a trap while loading is the end of the load: the strings are not asked for */
    static app_services_t in_russian;
    in_russian = SERVICES;
    in_russian.language = russian;
    CHECK(fake_kv_set("budget", "trap", "1", 1) == HOST_OK);
    logged[0] = '\0';
    CHECK(app_load("budget", HOST_API_VERSION, budget_wasm, sizeof budget_wasm, 0, &in_russian, &app) ==
          APP_OK);
    CHECK(app_has_stopped(app));
    CHECK(strstr(logged, "strings") == NULL);
    app_unload(app);
    CHECK(fake_kv_set("budget", "trap", "", 0) == HOST_OK);
    logged[0] = '\0';
    CHECK(app_load("budget", HOST_API_VERSION, budget_wasm, sizeof budget_wasm, 0, &in_russian, &app) ==
          APP_OK);
    CHECK(!app_has_stopped(app));
    CHECK(strstr(logged, "strings") != NULL);
    app_unload(app);
}

/* Handwritten Wasm keeps the compiler from replacing or combining bulk operations. */
static void wasm_uint(uint8_t *bytes, size_t *size, uint32_t value)
{
    do {
        uint8_t byte = value & 0x7f;
        value >>= 7;
        bytes[(*size)++] = byte | (value != 0 ? 0x80 : 0);
    } while (value != 0);
}

static void test_bulk_memory_budget(void)
{
    static const uint8_t prefix[] = {
        0, 0x61, 0x73, 0x6d, 1, 0, 0, 0,
        1, 5, 1, 0x60, 1, 0x7f, 0,
        3, 2, 1, 0,
        5, 4, 1, 1, 1, 1,
        7, 17, 1, 13, 'a','p','p','_','o','n','_','b','u','t','t','o','n', 0, 0
    };
    static const uint8_t fill[] = { 0x41, 0, 0x41, 0, 0x41, 0x80, 0x80, 4, 0xfc, 11, 0 };
    static const uint8_t copy[] = { 0x41, 0, 0x41, 0, 0x41, 0x80, 0x80, 4, 0xfc, 10, 0, 0 };
    enum { BULK_FILL, BULK_COPY, BULK_LOOP, SHORT_LOOP, BULK_INIT, SAMPLED_LOOP, BUDGET_CASE_COUNT };
    for (int kind = BULK_FILL; kind < BUDGET_CASE_COUNT; kind++) {
        uint8_t body[2048], wasm[4096];
        size_t body_size = 0, wasm_size = sizeof prefix;
        bool ordinary_loop = kind == SHORT_LOOP || kind == SAMPLED_LOOP;
        body[body_size++] = ordinary_loop ? 1 : 0;
        if (ordinary_loop) {
            /* 8192 turns reach the sampled clock; ten turns must not read it. */
            const uint8_t loop_start[] = { 1, 0x7f, 0x41,
                kind == SHORT_LOOP ? 0x80 : 0x8a, kind == SHORT_LOOP ? 0xc0 : 0x80, 0,
                0x21, 1, 3, 0x40 };
            memcpy(body + body_size, loop_start, sizeof loop_start);
            body_size += sizeof loop_start;
        }
        if (kind == BULK_LOOP) {
            body[body_size++] = 3;
            body[body_size++] = 0x40;
        }
        static const uint8_t init[] = { 0x41, 0, 0x41, 0, 0x41, 1, 0xfc, 8, 0, 0 };
        for (int i = 0; !ordinary_loop && i < 100; i++) {
            const uint8_t *operation = kind == BULK_COPY ? copy : kind == BULK_INIT ? init : fill;
            size_t length = kind == BULK_COPY ? sizeof copy : kind == BULK_INIT ? sizeof init : sizeof fill;
            memcpy(body + body_size, operation, length);
            body_size += length;
        }
        if (kind == BULK_LOOP) {
            body[body_size++] = 0x0c;
            body[body_size++] = 0;
            body[body_size++] = 0x0b;
        }
        if (ordinary_loop) {
            static const uint8_t loop_end[] = { 0x20, 1, 0x41, 1, 0x6b, 0x22, 1, 0x0d, 0, 0x0b };
            memcpy(body + body_size, loop_end, sizeof loop_end);
            body_size += sizeof loop_end;
        }
        /* Reaching this trap means work escaped interruption. */
        if (kind != SAMPLED_LOOP) body[body_size++] = 0;
        body[body_size++] = 0x0b;
        memcpy(wasm, prefix, sizeof prefix);
        if (kind == BULK_INIT) {
            static const uint8_t data_count[] = { 12, 1, 1 };
            memcpy(wasm + wasm_size, data_count, sizeof data_count);
            wasm_size += sizeof data_count;
        }
        wasm[wasm_size++] = 10;
        size_t encoded_body_size = body_size < 128 ? 1 : 2;
        wasm_uint(wasm, &wasm_size, (uint32_t)(1 + encoded_body_size + body_size));
        wasm[wasm_size++] = 1;
        wasm_uint(wasm, &wasm_size, (uint32_t)body_size);
        memcpy(wasm + wasm_size, body, body_size);
        wasm_size += body_size;
        if (kind == BULK_INIT) {
            static const uint8_t data[] = { 11, 4, 1, 1, 1, 0x7f };
            memcpy(wasm + wasm_size, data, sizeof data);
            wasm_size += sizeof data;
        }
        app_t *app = NULL;
        clock_runs = false;
        clock_ms = 0;
        CHECK(app_load("bulk", HOST_API_VERSION, wasm, wasm_size, 0, &SERVICES, &app) == APP_OK);
        CHECK(!app_has_stopped(app));
        clock_step_ms = 1000;
        clock_runs = true;
        app_button(app, 1, BTN_PRESSED);
        clock_runs = false;
        clock_step_ms = 1;
        if (kind == SAMPLED_LOOP) {
            CHECK(!app_has_stopped(app));
            CHECK(clock_ms == 2000); /* Only the handler's entry and return read the clock. */
        } else {
            CHECK(app_has_stopped(app));
            CHECK_STR(app_stop_reason(app), "it ran too long");
        }
        app_unload(app);
    }

    /* A return after the deadline must stop even without an interpreter yield. */
    uint8_t returning[sizeof prefix + 6];
    memcpy(returning, prefix, sizeof prefix);
    static const uint8_t empty_code[] = { 10, 4, 1, 2, 0, 0x0b };
    memcpy(returning + sizeof prefix, empty_code, sizeof empty_code);
    app_t *app = NULL;
    CHECK(app_load("return", HOST_API_VERSION, returning, sizeof returning, 0, &SERVICES, &app) == APP_OK);
    clock_step_ms = 4000;
    clock_runs = true;
    app_button(app, 1, BTN_PRESSED);
    clock_runs = false;
    clock_step_ms = 1;
    CHECK(app_has_stopped(app));
    CHECK_STR(app_stop_reason(app), "it ran too long");
    app_unload(app);
}

static void check_document_result(int count, int status, int length)
{
    int32_t result[5];
    CHECK(fake_kv_get("document", "document", result, sizeof result) == sizeof result);
    CHECK(result[0] == count && result[1] == 0 && result[2] == HOST_FORMAT_JSON &&
          result[3] == status && result[4] == length);
}

static app_t *load_document(bool dual, bool no_buffer)
{
    memset(kv_slots, 0, sizeof kv_slots);
    if (no_buffer) {
        uint8_t value = 1;
        fake_kv_set("document", "no_buffer", &value, sizeof value);
    }
    app_t *app = NULL;
    CHECK(app_load("document", HOST_API_VERSION,
                   dual ? document_dual_wasm : document_wasm,
                   dual ? sizeof document_dual_wasm : sizeof document_wasm,
                   0, &SERVICES, &app) == APP_OK);
    app_manifest_t manifest = { .response_format = HOST_FORMAT_JSON };
    manifest.poll_formats[0] = HOST_FORMAT_JSON;
    app_response_policy(app, &manifest);
    return app;
}

static void test_poll_allocation_delivery(void)
{
    memset(kv_slots, 0, sizeof kv_slots);
    app_t *app = load_probe();
    app_manifest_t manifest = { .poll_picks = 1 };
    app_response_policy(app, &manifest);
    app_answer_begins(app, NULL, NULL, HOST_FAILED_NO_MEMORY, -1);
    app_poll_failed(app, 0, HOST_FAILED_NO_MEMORY);
    app_answer_ends(app);
    int32_t failure[4];
    CHECK(fake_kv_get("probe", "poll_failed", failure, sizeof failure) == sizeof failure);
    CHECK(failure[0] == sizeof(host_answer_info_t) && failure[1] == HOST_FAILED_NO_MEMORY &&
          failure[2] == -1 && failure[3] == 0);
    app_unload(app);
}

static void test_document_delivery(void)
{
    const uint8_t body[] = "{\"value\":1234}";
    app_t *app = load_document(false, false);
    app_answer_begins(app, NULL, NULL, 200, -1);
    CHECK(!app_data(app, 0, body, sizeof body - 1, HOST_DATA_FIRST, 200));
    check_document_result(1, HOST_FAILED_TOO_LARGE, 0);
    CHECK(!app_data(app, 0, NULL, 0, HOST_DATA_FAILED, HOST_FAILED_NO_ANSWER));
    check_document_result(1, HOST_FAILED_TOO_LARGE, 0);
    int32_t metadata[4];
    CHECK(fake_kv_get("document", "metadata", metadata, sizeof metadata) == sizeof metadata);
    CHECK(metadata[0] == sizeof(host_answer_info_t) && metadata[1] == 200 &&
          metadata[2] == -1 && metadata[3] == HOST_ERR_ARGUMENT);
    app_unload(app);

    app = load_document(false, false);
    app_answer_begins(app, NULL, NULL, 200, -1);
    CHECK(!app_data(app, 0, (const uint8_t *)"{", 1, HOST_DATA_FIRST | HOST_DATA_LAST, 200));
    check_document_result(1, HOST_FAILED_NO_ANSWER, 0);
    CHECK(fake_kv_get("document", "metadata", metadata, sizeof metadata) == sizeof metadata);
    CHECK(metadata[1] == 200 && metadata[2] == -1);
    app_unload(app);

    app = load_document(false, true);
    CHECK(!app_data(app, 0, body, 1, HOST_DATA_FIRST, 200));
    check_document_result(1, HOST_FAILED_NOT_READY, 0);
    CHECK(!app_data(app, 0, body + 1, 1, HOST_DATA_LAST, 200));
    check_document_result(1, HOST_FAILED_NOT_READY, 0);
    app_unload(app);

    app = load_document(false, false);
    CHECK(!app_data_headers(app, 0, 403));
    check_document_result(1, 403, 0);
    CHECK(!app_data(app, 0, NULL, 0, HOST_DATA_FAILED, HOST_FAILED_NO_ANSWER));
    check_document_result(1, 403, 0);
    /* Settings suppress the old closing delivery; the refetch fails before FIRST. */
    CHECK(!app_data_headers(app, 0, 403));
    check_document_result(2, 403, 0);
    app_settings_changed(app);
    CHECK(!app_data(app, 0, NULL, 0, HOST_DATA_FAILED, HOST_FAILED_TIMEOUT));
    check_document_result(3, HOST_FAILED_TIMEOUT, 0);
    app_unload(app);

    app = load_document(true, false);
    CHECK(app_data_headers(app, 0, 403));
    int32_t result[5];
    CHECK(fake_kv_get("document", "piece", result, sizeof result) == HOST_ERR_NOT_FOUND);
    CHECK(fake_kv_get("document", "document", result, sizeof result) == HOST_ERR_NOT_FOUND);
    CHECK(app_data(app, 0, body, 2, HOST_DATA_FIRST, 403));
    CHECK(fake_kv_get("document", "document", result, sizeof result) == HOST_ERR_NOT_FOUND);
    CHECK(app_data(app, 0, body, 2, HOST_DATA_LAST, 403));
    check_document_result(1, 403, 0);
    CHECK(fake_kv_get("document", "piece", result, sizeof result) == sizeof result);
    CHECK(result[0] == 2 && result[2] == 2 && result[3] == HOST_DATA_LAST && result[4] == 403);
    CHECK(app_data(app, 0, body, sizeof body - 1, HOST_DATA_FIRST, 200));
    check_document_result(1, 403, 0);
    CHECK(app_data(app, 0, NULL, 0, HOST_DATA_FAILED, HOST_FAILED_TIMEOUT));
    check_document_result(2, HOST_FAILED_TIMEOUT, 0);
    app_unload(app);
}

static void test_builder_memory(const char *path, int32_t data_bytes)
{
    FILE *module = fopen(path, "rb");
    CHECK(module != NULL);
    if (module == NULL) {
        return;
    }
    CHECK(fseek(module, 0, SEEK_END) == 0);
    long size = ftell(module);
    rewind(module);
    uint8_t *bytes = malloc((size_t)size);
    CHECK(bytes != NULL && fread(bytes, 1, (size_t)size, module) == (size_t)size);
    fclose(module);
    app_t *app = NULL;
    CHECK(app_load("sizing", 3, bytes, (size_t)size, 0, &SERVICES, &app) == APP_OK);
    CHECK(app != NULL && !app_has_stopped(app));
    int32_t layout[4] = {0};
    CHECK(fake_kv_get("sizing", "m", layout, sizeof layout) == sizeof layout);
    CHECK(layout[0] == 8192);
    CHECK(layout[1] > 0 && layout[1] + (int32_t)sizeof layout <= 8192);
    CHECK(layout[2] == data_bytes && layout[3] == 17);
    app_unload(app);
    free(bytes);
}

static int stress_queued;
static int stress_attempts;
static int32_t stress_http_send(const char *app_name, const app_request_t *request)
{
    CHECK_STR(app_name, "stress");
    CHECK_STR(request->url, "http://bench/q");
    CHECK(request->method == HOST_HTTP_POST);
    CHECK(request->tag >= HOST_REQUEST_FIRST && request->tag <= HOST_REQUEST_LAST);
    CHECK(request->header_count == 1);
    CHECK_STR(request->headers[0].name, "X-A");
    CHECK_STR(request->headers[0].value, "1");
    CHECK(request->body_length == HOST_HTTP_BODY_MAX);
    stress_attempts++;
    if (stress_queued == HOST_HTTP_PENDING_MAX) return HOST_ERR_NO_SPACE;
    stress_queued++;
    return HOST_OK;
}

static void check_stress_counters(app_t *app, const char *counters)
{
    enum { WIDTH = 400, HEIGHT = 260 };
    uint8_t actual[WIDTH / 8 * HEIGHT], expected[WIDTH / 8 * HEIGHT];
    gfx_t frame, reference;
    gfx_init(&frame, actual, WIDTH, HEIGHT);
    gfx_init(&reference, expected, WIDTH, HEIGHT);
    gfx_clear(&frame, false);
    gfx_clear(&reference, false);
    app_draw(app, &frame);
    const gfx_font_t *font = gfx_font(GFX_FONT_SMALL);
    int row_y = HOST_UI_MARGIN + 5 * gfx_font_height(gfx_font(GFX_FONT_TEXT));
    CHECK(HOST_UI_MARGIN + gfx_text_font_width(font, counters) <= WIDTH - HOST_UI_MARGIN);
    CHECK(row_y + gfx_font_height(font) <= HEIGHT);
    gfx_text_font(&reference, HOST_UI_MARGIN, row_y + gfx_font_ascent(font), font, counters, true);
    CHECK(memcmp(actual + row_y * WIDTH / 8, expected + row_y * WIDTH / 8,
                 (size_t)gfx_font_height(font) * WIDTH / 8) == 0);
    CHECK(!app_has_stopped(app));
}

static void test_stress_package(const char *path)
{
    FILE *module = fopen(path, "rb");
    CHECK(module != NULL);
    if (module == NULL) return;
    CHECK(fseek(module, 0, SEEK_END) == 0);
    long size = ftell(module);
    CHECK(size > 0);
    rewind(module);
    uint8_t *bytes = size > 0 ? malloc((size_t)size) : NULL;
    CHECK(bytes != NULL);
    if (bytes == NULL) { fclose(module); return; }
    CHECK(fread(bytes, 1, (size_t)size, module) == (size_t)size);
    fclose(module);
    app_services_t services = SERVICES;
    services.http_send = stress_http_send;
    const char *modes[] = { "loop", "recurse", "http", "arena" };
    for (size_t index = 0; index < sizeof modes / sizeof modes[0]; index++) {
        const char *mode = modes[index];
        memset(kv_slots, 0, sizeof kv_slots);
        CHECK(fake_kv_set("stress", "mode", mode, (int32_t)strlen(mode)) == HOST_OK);
        CHECK(fake_kv_set("stress", "server", "http://bench", 12) == HOST_OK);
        clock_runs = false;
        clock_ms = FAKE_UPTIME_MS;
        clock_step_ms = 1;
        app_t *app = NULL;
        CHECK(app_load("stress", HOST_API_VERSION, bytes, (size_t)size, APP_GRANT_FILES,
                       &services, &app) == APP_OK);
        if (app == NULL) continue;
        CHECK(!app_has_stopped(app));
        stress_queued = stress_attempts = 0;
        clock_runs = true;
        app_focus(app);
        clock_runs = false;
        if (strcmp(mode, "loop") == 0) {
            CHECK(app_has_stopped(app));
            CHECK_STR(app_stop_reason(app), "it ran too long");
        } else if (strcmp(mode, "recurse") == 0) {
            CHECK(app_has_stopped(app));
            CHECK(strstr(app_stop_reason(app), "stack") != NULL);
        } else if (strcmp(mode, "http") == 0) {
            CHECK(stress_queued == HOST_HTTP_PENDING_MAX);
            CHECK(stress_attempts == HOST_HTTP_PENDING_MAX + 1);
            check_stress_counters(app, "ticks 0 requests 17 refused 1");
            app_data(app, HOST_REQUEST_FIRST, NULL, 0, HOST_DATA_FIRST, 200);
            CHECK(stress_attempts == HOST_HTTP_PENDING_MAX + 1);
            stress_queued--;
            app_data(app, HOST_REQUEST_FIRST, NULL, 0, HOST_DATA_LAST, 200);
            CHECK(stress_queued == HOST_HTTP_PENDING_MAX);
            CHECK(stress_attempts == HOST_HTTP_PENDING_MAX + 3);
            check_stress_counters(app, "ticks 0 requests 19 refused 2");
            stress_queued--;
            app_data(app, HOST_REQUEST_FIRST, NULL, 0, HOST_DATA_FAILED, HOST_FAILED_NO_ANSWER);
            CHECK(stress_queued == HOST_HTTP_PENDING_MAX);
            CHECK(stress_attempts == HOST_HTTP_PENDING_MAX + 5);
            check_stress_counters(app, "ticks 0 requests 21 refused 3");
        } else {
            check_stress_counters(app, "ticks 0 requests 0 refused 0");
            app_settings_changed(app);
            check_stress_counters(app, "ticks 0 requests 0 refused 1");
            app_settings_changed(app);
            check_stress_counters(app, "ticks 0 requests 0 refused 2");
        }
        CHECK(stress_attempts == 0 || strcmp(mode, "http") == 0);
        app_unload(app);
    }
    free(bytes);
}

int main(int argc, char **argv)
{
    if (argc == 4 && strcmp(argv[1], "--builder-memory") == 0) {
        test_builder_memory(argv[2], (int32_t)strtol(argv[3], NULL, 10));
        REPORT();
    }
    if (argc == 3) {
        test_stress_package(argv[1]);
        test_stress_package(argv[2]);
        REPORT();
    }
    CHECK(argc == 1);
    test_poll_allocation_delivery();
    test_document_delivery();
    test_screen_inversion();
    test_probe_contract();
    app_t *text_probe = load_probe();
    draw(text_probe);
    CHECK(gfx_get(&surface, 14, ROW_SCENE_STATE));
    app_unload(text_probe);

    test_manifest_geometry_in_load();
    test_scene_state();
    test_response_headers();
    test_byte_page_memory();
    probe_bytes = frozen_probe_wasm;
    probe_size = sizeof frozen_probe_wasm;
    /* Frozen prerelease packages remain import/layout fixtures. Their button
     * values and conversion range are replaced by the release contract. */
    app_t *frozen = load_probe();
    draw(frozen);
    CHECK(!app_has_stopped(frozen));
    app_unload(frozen);
    test_frozen_hello_state();
    probe_bytes = probe_wasm;
    probe_size = sizeof probe_wasm;
    test_host_calls_count_against_the_budget();
    test_bulk_memory_budget();
    REPORT();
}
