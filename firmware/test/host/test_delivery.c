/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "app_delivery.h"
#include "answer_headers.h"
#include "app_scene.h"
#include "app_store.h"
#include "host_api.h"
#include "host.h"
#include "wm.h"
#include "question_press.h"
#include "m3_area.h"
#include "notice.h"
#include "words.h"
#include "gfx_icons.h"

#include "tinytest.h"

/* Keep the host interpreter on its native allocator, while exercising the
 * firmware's area-availability guard and map scratch path. */
static bool area_available = true;
bool m3_area_available(void) { return area_available; }
void m3_area_reset(void) {}
void *m3_area_take(size_t size) { return malloc(size); }
void m3_area_give(void *memory) { free(memory); }

static bool refuse_module_memory;
static void *module_malloc(size_t size)
{
    if (refuse_module_memory) {
        refuse_module_memory = false;
        errno = 0; /* as ESP-IDF's malloc: errno is left alone, read_file must name the cause */
        return NULL;
    }
    return malloc(size);
}

#define malloc module_malloc
#include "../../components/appstore/app_store.c"
#undef malloc

/* An answer from the network on its way to the application it is for: a
 * real module (the probe of test_app) installed in a real folder, a
 * window manager, and a stand-in for the menu. */

static int64_t no_time(void) { return 0; }
static int32_t no_offset_at(int64_t unix_time)
{
    (void)unix_time;
    return 0;
}

static int32_t no_offset(void) { return 0; }
static void no_log(int level, const char *text, int length) { (void)level; (void)text; (void)length; }
static void no_timer(int id, int32_t milliseconds) { (void)id; (void)milliseconds; }
static int32_t no_kv_get(const char *app, const char *key, void *value, int32_t capacity)
{
    (void)app; (void)key; (void)value; (void)capacity;
    return HOST_ERR_NOT_FOUND;
}
static int32_t no_kv_set(const char *app, const char *key, const void *value, int32_t length)
{
    (void)app; (void)key; (void)value; (void)length;
    return HOST_OK;
}
static bool no_poll_now(const char *app, int poll) { (void)app; (void)poll; return false; }
static int32_t no_interval(const char *app, int poll, uint32_t s) { (void)app; (void)poll; (void)s; return HOST_ERR_ARGUMENT; }
static bool no_alarm(const char *app, int id, int64_t when, bool is_absolute)
{
    (void)app;
    (void)id;
    (void)when;
    (void)is_absolute;
    return false;
}
static int32_t no_file_write(const char *app, const char *file, int32_t offset, const void *bytes,
                             int32_t length)
{
    (void)app; (void)file; (void)offset; (void)bytes; (void)length;
    return HOST_ERR_FAILED;
}
static int32_t no_request(const char *app, const app_request_t *request)
{
    (void)app; (void)request;
    return HOST_ERR_FAILED;
}
static int32_t no_file_size(const char *app, const char *file)
{
    (void)app; (void)file;
    return HOST_ERR_NOT_FOUND;
}
static int32_t no_file_read(const char *app, const char *file, int32_t offset, void *bytes,
                            int32_t capacity)
{
    (void)app; (void)file; (void)offset; (void)bytes; (void)capacity;
    return HOST_ERR_NOT_FOUND;
}
static bool always_dithers(void) { return true; }
static const app_services_t SERVICES = {
    .unix_time = no_time, .time_zone_offset = no_offset, .time_zone_offset_at = no_offset_at, .uptime_ms = no_time, .log_write = no_log,
    .timer_set = no_timer, .kv_get = no_kv_get, .kv_set = no_kv_set,
    .dithers = always_dithers, .poll_run = no_poll_now, .poll_set_interval = no_interval,
    .alarm_set = no_alarm, .http_send = no_request, .file_write = no_file_write,
    .file_size = no_file_size, .file_read = no_file_read,
};

typedef struct {
    scene_t scene;
    int     entered, left;
} menu_t;

static void menu_enter(scene_t *scene) { ((menu_t *)scene)->entered++; }
static void menu_leave(scene_t *scene) { ((menu_t *)scene)->left++; }
static scene_reaction_t menu_button(scene_t *scene, scene_button_t button, scene_press_t press)
{
    (void)scene; (void)button; (void)press;
    scene_reaction_t ignored = { SCENE_IGNORED, NULL };
    return ignored;
}
static void menu_draw(scene_t *scene, gfx_t *surface) { (void)scene; (void)surface; }
static const scene_ops_t MENU_OPS = { .enter = menu_enter, .leave = menu_leave,
                                      .button = menu_button, .draw = menu_draw };
static void no_header(gfx_t *surface, const scene_t *scene, void *context)
{
    (void)surface;
    (void)scene;
    (void)context;
}

static char                 root[64];
static menu_t               menu;
static scene_t             *probe;
wm_t host_wm;
app_delivery_state_t host_delivery;
#define wm host_wm
#define state host_delivery
static scene_t *document_scene;
scene_t *launcher_app_scene(const char *name)
{
    return strcmp(name, "document") == 0 ? document_scene : NULL;
}
const char *launcher_app_name(const scene_t *scene)
{
    return scene == document_scene ? "document" : NULL;
}
int64_t esp_timer_get_time(void) { return 0; }
void poller_answer_was_abandoned(const char *name, int request) { (void)name; (void)request; }
void host_end_background_answer(void)
{
    CHECK(false); /* These cases deliver one owner's answer at a time. */
}

static void copy_file(const char *from, const char *to)
{
    FILE *source = fopen(from, "rb"), *target = fopen(to, "wb");
    if (source == NULL || target == NULL) {
        perror(from);
        exit(1);
    }
    char buffer[4096];
    size_t count;
    while ((count = fread(buffer, 1, sizeof buffer, source)) > 0) {
        fwrite(buffer, 1, count, target);
    }
    fclose(source);
    fclose(target);
}

static void install_probe(void)
{
    strcpy(root, "/tmp/perinkpheral-delivery-XXXXXX");
    if (mkdtemp(root) == NULL) {
        perror("mkdtemp");
        exit(1);
    }
    char path[128];
    snprintf(path, sizeof path, "%s/probe", root);
    mkdir(path, 0755);
    snprintf(path, sizeof path, "%s/probe/app.wasm", root);
    copy_file("../../../sdk/abi/3/probe/app.wasm", path);
    snprintf(path, sizeof path, "%s/probe/manifest.json", root);
    copy_file("../../../sdk/abi/3/probe/manifest.json", path);

    app_manifest_t manifest;
    CHECK(app_store_list(root, &manifest, 1) == 1);
    probe = app_scene_create(&manifest, root, &SERVICES);
    CHECK(probe != NULL);
}

static void start_on(scene_t *first)
{
    /* What the test before left on the screen goes the way a scene goes. */
    if (app_scene_app(probe) != NULL) {
        probe->ops->leave(probe);
    }
    memset(&menu, 0, sizeof menu);
    menu.scene.ops = &MENU_OPS;
    wm_init(&wm, &menu.scene, first, no_header, NULL);
    wm_shown(&wm);
    app_delivery_init(&state);
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

static void deliver_values(int64_t now_ms)
{
    app_delivery_t delivery = { .kind = APP_DELIVERY_VALUES, .request = 3, .http_status = 200,
                                .picked = picked_values() };
    app_deliver(&state, &wm, probe, &delivery, now_ms);
}

/* The probe asks for the screen once it has all 2500 bytes of request 1. */
static void deliver_piece(size_t length, int flags, int64_t now_ms)
{
    static uint8_t bytes[HOST_DATA_CHUNK_MAX];
    memset(bytes, 3, sizeof bytes);
    app_delivery_t delivery = { .kind = APP_DELIVERY_PIECE, .request = 1, .http_status = 200,
                                .data = bytes, .length = length, .flags = flags };
    app_deliver(&state, &wm, probe, &delivery, now_ms);
}

static void test_manifest_ownership(void)
{
    app_manifest_t manifest, temporary;
    CHECK(app_store_list(root, &manifest, 1) == 1);
    temporary = manifest;
    size_t size;
    uint8_t *module = app_store_read_module(root, manifest.name, &size);
    CHECK(module != NULL);
    scene_t *scenes[] = {
        app_scene_create(&temporary, root, &SERVICES),
        app_scene_create_in_place(&temporary, module, size, &SERVICES),
        app_scene_borrow(&manifest, root, NULL, 0, &SERVICES),
        app_scene_borrow(&manifest, NULL, module, size, &SERVICES),
    };
    memset(&temporary, 0, sizeof temporary);
    for (size_t i = 0; i < sizeof scenes / sizeof scenes[0]; i++) {
        CHECK(scenes[i] != NULL);
        start_on(scenes[i]);
        CHECK(app_scene_app(scenes[i]) != NULL);
        wm_close(&wm, scenes[i]);
        app_scene_destroy(scenes[i]);
    }
    free(module);
}

static void test_missing_application_area(void)
{
    start_on(&menu.scene);
    size_t module_size;
    uint8_t *module = app_store_read_module(root, "probe", &module_size);
    CHECK(module != NULL);
    area_available = false;
    app_t *loaded = NULL;
    CHECK(app_load("probe", 3, module, module_size, 0, &SERVICES, &loaded) == APP_ERR_NO_AREA);
    CHECK(loaded == NULL);
    free(module);

    const char *languages[] = { "en", "ru" };
    for (size_t index = 0; index < sizeof languages / sizeof languages[0]; index++) {
        words_use(languages[index]);
        start_on(probe);
        CHECK(app_scene_app(probe) == NULL);
        uint8_t actual[400 * 260 / 8], expected[sizeof actual];
        gfx_t surface;
        gfx_init(&surface, actual, 400, 260);
        gfx_clear(&surface, false);
        probe->ops->draw(probe, &surface);
        gfx_init(&surface, expected, 400, 260);
        gfx_clear(&surface, false);
        const notice_t notice = {
            .first = "Probe",
            .second = word(WORD_CANNOT_START),
            .detail = word(WORD_APP_MEMORY_UNAVAILABLE),
            .icon = GFX_ICON_WARNING,
            .back = word(WORD_CLOSE),
        };
        notice_draw(&surface, &notice);
        CHECK(memcmp(actual, expected, sizeof actual) == 0);
    }
    words_use("en");
    start_on(&menu.scene);
    area_available = true;
    start_on(probe);
    CHECK(app_scene_app(probe) != NULL);
}

static void test_on_the_screen(void)
{
    start_on(probe);
    CHECK(app_scene_app(probe) != NULL);
    deliver_values(0);
    /* the probe asked for a redraw, and nobody pressed anything */
    CHECK(wm_pending_change(&wm) == WM_CHANGED_BY_DATA);
    CHECK(wm.current == probe && state.in_background == NULL);

    /* asking for the screen it already has changes nothing */
    wm_shown(&wm);
    deliver_piece(HOST_DATA_CHUNK_MAX, HOST_DATA_FIRST, 0);
    deliver_piece(HOST_DATA_CHUNK_MAX, 0, 0);
    deliver_piece(2500 - 2048, HOST_DATA_LAST, 0);
    CHECK(wm.current == probe && wm_pending_change(&wm) != WM_CHANGED_BY_SWITCH);
}

static void test_off_the_screen(void)
{
    start_on(&menu.scene);
    CHECK(menu.entered == 1 && app_scene_app(probe) == NULL);
    deliver_values(0);
    /* the menu let go and came back; the probe was loaded only for the call */
    CHECK(menu.left == 1 && menu.entered == 2);
    CHECK(wm.current == &menu.scene && !wm.suspended);
    CHECK(app_scene_app(probe) == NULL && state.in_background == NULL);
}

static void count_delivery(void *context)
{
    (*(int *)context)++;
}

static void test_foreground_stream_interrupted_by_another_owner(void)
{
    app_manifest_t manifest;
    CHECK(app_store_list(root, &manifest, 1) == 1);
    scene_t *other = app_scene_create(&manifest, root, &SERVICES);
    CHECK(other != NULL);
    const int requests[] = { 1, HOST_REQUEST_FIRST };
    const int endings[] = { HOST_DATA_LAST, HOST_DATA_FAILED };
    for (size_t index = 0; index < sizeof requests / sizeof requests[0]; index++) {
        start_on(probe);
        int calls = 0;
        uint8_t byte = 3;
        app_delivery_t piece = { .kind = APP_DELIVERY_PIECE, .request = requests[index],
            .http_status = 200, .data = &byte, .length = 1, .flags = HOST_DATA_FIRST,
            .loaded = count_delivery, .loaded_context = &calls };
        CHECK(app_deliver(&state, &wm, probe, &piece, 0).owner == NULL);
        uint64_t instance = app_scene_instance(probe);
        CHECK(instance != 0 && calls == 1 && state.answer_owner == probe);

        /* Ordinary input and its own alarm keep the receiving instance. */
        app_abandoned_t abandoned;
        app_delivery_abort(&state, &wm, 1, &abandoned);
        CHECK(abandoned.owner == NULL && app_scene_instance(probe) == instance);
        app_delivery_t alarm = { .kind = APP_DELIVERY_ALARM };
        CHECK(app_deliver(&state, &wm, probe, &alarm, 2).owner == NULL);
        CHECK(app_scene_instance(probe) == instance);

        abandoned = app_deliver(&state, &wm, other, &alarm, 3);
        CHECK(abandoned.owner == probe && abandoned.request == requests[index]);
        CHECK(wm.current == probe && !wm.suspended && app_scene_app(other) == NULL);
        CHECK(app_scene_instance(probe) != instance);
        CHECK(state.answer_instance == 0);

        piece.flags = 0;
        CHECK(app_deliver(&state, &wm, probe, &piece, 4).owner == NULL);
        CHECK(calls == 1);
        piece.flags = endings[index];
        CHECK(app_deliver(&state, &wm, probe, &piece, 5).owner == NULL);
        CHECK(calls == 1 && state.answer_owner == NULL);
        /* The retry starts a new response in the restored instance. */
        piece.flags = HOST_DATA_FIRST | HOST_DATA_LAST;
        CHECK(app_deliver(&state, &wm, probe, &piece, 6).owner == NULL);
        CHECK(calls == 2 && state.answer_owner == NULL);
    }
    app_scene_destroy(other);
}

static void test_foreground_stream_interrupted_by_navigation(void)
{
    start_on(probe);
    int calls = 0;
    app_delivery_t piece = { .kind = APP_DELIVERY_PIECE, .request = 1, .http_status = 200,
        .flags = HOST_DATA_FIRST, .loaded = count_delivery, .loaded_context = &calls };
    app_abandoned_t taken = app_deliver(&state, &wm, probe, &piece, 0);
    CHECK(taken.owner == NULL && !taken.rest_unwanted); /* the probe reads pieces */
    uint64_t instance = app_scene_instance(probe);
    wm_button(&wm, SCENE_BUTTON_BACK, SCENE_PRESS_LONG);
    CHECK(app_scene_instance(probe) == 0);
    wm_open(&wm, probe);
    CHECK(app_scene_instance(probe) != instance);
    piece.flags = 0;
    app_abandoned_t abandoned = app_deliver(&state, &wm, probe, &piece, 1);
    CHECK(abandoned.owner == probe && abandoned.request == 1 && calls == 1);
    CHECK(abandoned.rest_unwanted); /* the instance that took the beginning is gone */
    piece.flags = HOST_DATA_LAST;
    CHECK(app_deliver(&state, &wm, probe, &piece, 2).owner == NULL);
    CHECK(calls == 1);
    piece.flags = HOST_DATA_FAILED;
    CHECK(app_deliver(&state, &wm, probe, &piece, 3).owner == NULL);
    CHECK(calls == 2); /* a new request can fail before it has a FIRST */
}

/* The failure that ends an answer nobody took the rest of goes to the instance taking it and
 * to no other: after the application list was reloaded mid-answer, it is not a new failure. */
static void test_closing_failure_reaches_only_its_answer(void)
{
    for (int is_closing = 0; is_closing < 2; is_closing++) {
        start_on(probe);
        int calls = 0;
        app_delivery_t piece = { .kind = APP_DELIVERY_PIECE, .request = 1, .http_status = 200,
            .flags = HOST_DATA_FIRST, .loaded = count_delivery, .loaded_context = &calls };
        CHECK(app_deliver(&state, &wm, probe, &piece, 0).owner == NULL && calls == 1);
        state.answer_owner = NULL; /* what a reload of the application list does to the answer */
        piece.flags = 0;
        CHECK(app_deliver(&state, &wm, probe, &piece, 1).rest_unwanted && calls == 1);
        app_delivery_t ending = { .kind = APP_DELIVERY_PIECE, .request = 1,
            .http_status = HOST_FAILED_NO_ANSWER, .flags = HOST_DATA_FAILED,
            .is_closing = is_closing, .loaded = count_delivery, .loaded_context = &calls };
        app_deliver(&state, &wm, probe, &ending, 2);
        CHECK(calls == (is_closing ? 1 : 2)); /* a failure of its own would be told */
        CHECK(state.answer_owner == NULL && !wm.suspended);
    }
    /* To the instance still taking the answer it is the answer's end. */
    start_on(&menu.scene);
    int calls = 0;
    app_delivery_t piece = { .kind = APP_DELIVERY_PIECE, .request = 1, .http_status = 200,
        .flags = HOST_DATA_FIRST, .loaded = count_delivery, .loaded_context = &calls };
    app_deliver(&state, &wm, probe, &piece, 0);
    CHECK(calls == 1 && state.in_background == probe && wm.suspended);
    app_delivery_t ending = { .kind = APP_DELIVERY_PIECE, .request = 1,
        .http_status = HOST_FAILED_NO_ANSWER, .flags = HOST_DATA_FAILED, .is_closing = true,
        .loaded = count_delivery, .loaded_context = &calls };
    app_deliver(&state, &wm, probe, &ending, 1);
    CHECK(calls == 2 && state.in_background == NULL && !wm.suspended && state.answer_owner == NULL);
}

static void test_stream_owner_cannot_load(void)
{
    start_on(probe);
    app_manifest_t manifest;
    CHECK(app_store_list(root, &manifest, 1) == 1);
    strcpy(manifest.name, "missing");
    scene_t *missing = app_scene_create(&manifest, root, &SERVICES);
    CHECK(missing != NULL);
    static const uint8_t bad_module[] = { 0 };
    scene_t *broken = app_scene_create_in_place(&manifest, bad_module, sizeof bad_module, &SERVICES);
    CHECK(broken != NULL);
    size_t module_size;
    uint8_t *module = app_store_read_module(root, "probe", &module_size);
    CHECK(module != NULL);
    manifest.abi = HOST_API_VERSION + 1;
    scene_t *incompatible = app_scene_create_in_place(&manifest, module, module_size, &SERVICES);
    CHECK(incompatible != NULL);
    scene_t *owners[] = { missing, broken, incompatible };
    int calls = 0;
    app_delivery_t piece = { .kind = APP_DELIVERY_PIECE, .request = 1,
                            .http_status = 200, .loaded = count_delivery, .loaded_context = &calls };
    for (size_t index = 0; index < sizeof owners / sizeof owners[0]; index++) {
        scene_t *owner = owners[index];
        for (int poll = 0; poll < 2; poll++) {
            piece.flags = HOST_DATA_FIRST;
            CHECK(app_deliver(&state, &wm, owner, &piece, 0).owner == NULL);
            CHECK(wm.current == probe && !wm.suspended && state.in_background == NULL);
            CHECK(app_scene_app(probe) != NULL && app_scene_instance(owner) == 0);
            uint64_t foreground_instance = app_scene_instance(probe);
            piece.flags = 0;
            CHECK(app_deliver(&state, &wm, owner, &piece, 1).owner == NULL);
            piece.flags = HOST_DATA_LAST;
            CHECK(app_deliver(&state, &wm, owner, &piece, 2).owner == NULL);
            CHECK(!wm.suspended && state.answer_owner == NULL && calls == 0);
            CHECK(app_scene_instance(probe) == foreground_instance);
        }
        app_scene_destroy(owner);
    }
    free(module);
}

static void test_failure_starts_a_different_answer(void)
{
    app_manifest_t manifest;
    CHECK(app_store_list(root, &manifest, 1) == 1);
    scene_t *other = app_scene_create(&manifest, root, &SERVICES);
    CHECK(other != NULL);
    for (int different_owner = 0; different_owner < 2; different_owner++) {
        start_on(probe);
        deliver_piece(HOST_DATA_CHUNK_MAX, HOST_DATA_FIRST, 0);
        wm_open(&wm, &menu.scene);
        app_abandoned_t abandoned;
        app_delivery_abort(&state, &wm, 1, &abandoned);
        CHECK(abandoned.owner == probe && state.answer_owner == probe);
        int calls = 0;
        app_delivery_t failure = { .kind = APP_DELIVERY_PIECE,
            .request = different_owner ? 1 : 2, .flags = HOST_DATA_FAILED,
            .http_status = HOST_FAILED_NO_NETWORK,
            .loaded = count_delivery, .loaded_context = &calls };
        CHECK(app_deliver(&state, &wm, different_owner ? other : probe, &failure, 2).owner == NULL);
        CHECK(calls == 1 && state.answer_owner == NULL && !wm.suspended);
    }
    app_scene_destroy(other);
}

static void test_background_load_memory_failure(void)
{
    for (int reading_module = 0; reading_module < 2; reading_module++) {
        start_on(&menu.scene);
        size_t module_size;
        uint8_t *module = app_store_read_module(root, "probe", &module_size);
        CHECK(module != NULL);
        app_t *occupying = NULL;
        if (reading_module) {
            refuse_module_memory = true;
        } else {
            CHECK(app_load("occupying", HOST_API_VERSION, module, module_size,
                           APP_GRANT_SYSTEM | APP_GRANT_FILES, &SERVICES, &occupying) == APP_OK);
        }
        int calls = 0;
        app_delivery_t piece = { .kind = APP_DELIVERY_PIECE, .request = 1,
            .flags = HOST_DATA_FIRST, .http_status = 200,
            .loaded = count_delivery, .loaded_context = &calls };
        app_abandoned_t abandoned = app_deliver(&state, &wm, probe, &piece, 0);
        CHECK(abandoned.owner == probe && abandoned.request == 1 && abandoned.rest_unwanted);
        CHECK(!refuse_module_memory && !wm.suspended && state.in_background == NULL);
        CHECK(calls == 0 && state.answer_owner == probe && state.answer_instance == 0);
        if (occupying != NULL) {
            app_unload(occupying);
        }
        free(module);
        piece.flags = 0;
        CHECK(app_deliver(&state, &wm, probe, &piece, 1).owner == NULL);
        piece.flags = HOST_DATA_LAST;
        CHECK(app_deliver(&state, &wm, probe, &piece, 2).owner == NULL);
        CHECK(calls == 0 && state.answer_owner == NULL && !wm.suspended);
        piece.flags = HOST_DATA_FIRST | HOST_DATA_LAST;
        CHECK(app_deliver(&state, &wm, probe, &piece, 3).owner == NULL);
        CHECK(calls == 1 && state.answer_owner == NULL && !wm.suspended);
    }
}

static void test_cut_poll_settings_allow_a_failed_refetch(void)
{
    start_on(&menu.scene);
    int calls = 0;
    app_delivery_t piece = { .kind = APP_DELIVERY_PIECE, .request = 1,
        .flags = HOST_DATA_FIRST, .http_status = 200,
        .loaded = count_delivery, .loaded_context = &calls };
    CHECK(app_deliver(&state, &wm, probe, &piece, 0).owner == NULL);
    app_abandoned_t abandoned;
    app_delivery_abort(&state, &wm, 1, &abandoned);
    CHECK(abandoned.owner == probe && abandoned.request == 1);
    CHECK(state.answer_owner == probe && state.answer_instance == 0 && state.in_background == NULL);
    app_delivery_t settings = { .kind = APP_DELIVERY_SETTINGS };
    CHECK(app_deliver(&state, &wm, probe, &settings, 2).owner == NULL);
    CHECK(state.answer_owner == NULL && state.answer_instance == 0);
    /* poller_hurry suppresses the old answer, including LAST. */
    piece.flags = HOST_DATA_FAILED;
    piece.http_status = HOST_FAILED_NO_ANSWER;
    CHECK(app_deliver(&state, &wm, probe, &piece, 3).owner == NULL);
    CHECK(calls == 2 && !wm.suspended && state.in_background == NULL);
}

static void test_settings_allow_a_failed_refetch(void)
{
    const int requests[] = { 1, HOST_REQUEST_FIRST };
    for (int on_screen = 0; on_screen < 2; on_screen++) {
        for (size_t index = 0; index < sizeof requests / sizeof requests[0]; index++) {
            start_on(on_screen ? probe : &menu.scene);
            int calls = 0;
            app_delivery_t piece = { .kind = APP_DELIVERY_PIECE, .request = requests[index],
                .flags = HOST_DATA_FIRST, .http_status = 200,
                .loaded = count_delivery, .loaded_context = &calls };
            CHECK(app_deliver(&state, &wm, probe, &piece, 0).owner == NULL);
            uint64_t instance = app_scene_instance(probe);
            app_delivery_t settings = { .kind = APP_DELIVERY_SETTINGS };
            CHECK(app_deliver(&state, &wm, probe, &settings, 1).owner == NULL);
            if (on_screen) {
                CHECK(app_scene_instance(probe) == instance);
            }
            if (requests[index] < HOST_REQUEST_FIRST) {
                CHECK(state.answer_owner == NULL && state.answer_instance == 0);
            } else {
                CHECK(state.answer_owner == probe && state.answer_instance == instance);
            }
            /* poller_hurry suppresses the invalidated poll, including LAST. */
            piece.flags = HOST_DATA_FAILED;
            piece.http_status = HOST_FAILED_NO_ANSWER;
            CHECK(app_deliver(&state, &wm, probe, &piece, 2).owner == NULL);
            CHECK(calls == 2 && state.answer_owner == NULL && !wm.suspended);
        }
    }
}

static void test_asks_for_the_screen(void)
{
    start_on(&menu.scene);
    deliver_piece(HOST_DATA_CHUNK_MAX, HOST_DATA_FIRST, 1000);
    /* between the first piece and the last the owner stays loaded */
    CHECK(wm.suspended && state.in_background == probe && app_scene_app(probe) != NULL);
    CHECK(menu.left == 1 && menu.entered == 1);
    deliver_piece(HOST_DATA_CHUNK_MAX, 0, 1000);
    CHECK(state.in_background == probe);
    deliver_piece(2500 - 2048, HOST_DATA_LAST, 1000);
    /* it asked, and got the screen: the menu does not come back */
    CHECK(wm.current == probe && !wm.suspended && state.in_background == NULL);
    CHECK(wm_pending_change(&wm) == WM_CHANGED_BY_SWITCH);
    CHECK(menu.entered == 1);
    CHECK(app_scene_app(probe) != NULL);
}

static void test_not_twice_in_a_row(void)
{
    start_on(&menu.scene);
    deliver_piece(2500, HOST_DATA_FIRST | HOST_DATA_LAST, 1000); /* too large: not delivered */
    CHECK(wm.current == &menu.scene);

    deliver_piece(HOST_DATA_CHUNK_MAX, HOST_DATA_FIRST, 1000);
    deliver_piece(HOST_DATA_CHUNK_MAX, 0, 1000);
    deliver_piece(2500 - 2048, HOST_DATA_LAST, 1000);
    CHECK(wm.current == probe);

    /* the user goes back to the menu; the next answer comes a moment later */
    wm_button(&wm, SCENE_BUTTON_BACK, SCENE_PRESS_LONG);
    CHECK(wm.current == &menu.scene);
    int64_t too_soon = 1000 + APP_FOCUS_MIN_GAP_MS - 1;
    deliver_piece(HOST_DATA_CHUNK_MAX, HOST_DATA_FIRST, too_soon);
    deliver_piece(HOST_DATA_CHUNK_MAX, 0, too_soon);
    deliver_piece(2500 - 2048, HOST_DATA_LAST, too_soon);
    CHECK(wm.current == &menu.scene && !wm.suspended);

    int64_t late_enough = 1000 + APP_FOCUS_MIN_GAP_MS;
    deliver_piece(HOST_DATA_CHUNK_MAX, HOST_DATA_FIRST, late_enough);
    deliver_piece(HOST_DATA_CHUNK_MAX, 0, late_enough);
    deliver_piece(2500 - 2048, HOST_DATA_LAST, late_enough);
    CHECK(wm.current == probe);
}

/* Somebody pressing buttons, or a question waiting for its answer, keeps the screen. */
static void test_not_from_under_the_user(void)
{
    start_on(&menu.scene);
    app_delivery_note_press(&state, 5000);
    int64_t pressed_lately = 5000 + APP_FOCUS_AFTER_PRESS_MS - 1;
    deliver_piece(HOST_DATA_CHUNK_MAX, HOST_DATA_FIRST, pressed_lately);
    deliver_piece(HOST_DATA_CHUNK_MAX, 0, pressed_lately);
    deliver_piece(2500 - 2048, HOST_DATA_LAST, pressed_lately);
    CHECK(wm.current == &menu.scene && !wm.suspended);

    menu.scene.question = 7; /* the screen asks something */
    int64_t later = 5000 + APP_FOCUS_AFTER_PRESS_MS;
    deliver_piece(HOST_DATA_CHUNK_MAX, HOST_DATA_FIRST, later);
    deliver_piece(HOST_DATA_CHUNK_MAX, 0, later);
    deliver_piece(2500 - 2048, HOST_DATA_LAST, later);
    CHECK(wm.current == &menu.scene);

    menu.scene.question = 0;
    deliver_piece(HOST_DATA_CHUNK_MAX, HOST_DATA_FIRST, later);
    deliver_piece(HOST_DATA_CHUNK_MAX, 0, later);
    deliver_piece(2500 - 2048, HOST_DATA_LAST, later);
    CHECK(wm.current == probe);
}

static void test_a_request_that_broke_off(void)
{
    start_on(&menu.scene);
    deliver_piece(HOST_DATA_CHUNK_MAX, HOST_DATA_FIRST, 0);
    CHECK(wm.suspended);
    deliver_piece(0, HOST_DATA_FAILED, 0);
    CHECK(!wm.suspended && wm.current == &menu.scene && state.in_background == NULL);
    CHECK(app_scene_app(probe) == NULL);
}

static void test_something_else_in_the_middle_of_an_answer(void)
{
    start_on(&menu.scene);
    deliver_piece(HOST_DATA_CHUNK_MAX, HOST_DATA_FIRST, 0);
    app_t *taking = app_scene_app(probe);
    CHECK(taking != NULL && state.is_mid_answer);
    /* Its alarm fires while the answer is still coming. */
    app_delivery_t alarm = { .kind = APP_DELIVERY_ALARM };
    app_deliver(&state, &wm, probe, &alarm, 0);
    /* the same instance goes on taking the answer */
    CHECK(app_scene_app(probe) == taking && state.in_background == probe && wm.suspended);
    deliver_piece(HOST_DATA_CHUNK_MAX, 0, 0);
    deliver_piece(2500 - 2048, HOST_DATA_LAST, 0);
    /* whole, and it asked for the screen: it had all 2500 bytes */
    CHECK(wm.current == probe && state.in_background == NULL && !state.is_mid_answer);
}

static void test_settings_document_registration(void)
{
    const int requests[] = {2, HOST_REQUEST_FIRST};
    for (unsigned i = 0; i < sizeof requests / sizeof requests[0]; i++) {
        start_on(probe);
        host_answer_info_t response = {.content_type = "application/json"};
        app_delivery_t piece = {.kind = APP_DELIVERY_PIECE,
                                .request = requests[i],
                                .http_status = 200,
                                .response = &response,
                                .flags = HOST_DATA_FIRST,
                                .data = (const uint8_t *)"{",
                                .length = 1};
        app_deliver(&state, &wm, probe, &piece, 0);
        app_delivery_t settings = {.kind = APP_DELIVERY_SETTINGS};
        app_deliver(&state, &wm, probe, &settings, 1);
        static uint8_t bits[400 * 300 / 8];
        gfx_t surface;
        gfx_init(&surface, bits, 400, 300);
        app_draw(app_scene_app(probe), &surface);
        /* An invalidated manifest poll releases its buffer. A request from
         * code continues across the settings event and retains ownership. */
        CHECK(gfx_get(&surface, i == 0 ? 10 : 12, 61));
        piece.flags = HOST_DATA_LAST;
        piece.data = (const uint8_t *)"}";
        app_deliver(&state, &wm, probe, &piece, 2);
    }
}

static void test_settings_discard_an_unfinished_poll(void)
{
    start_on(&menu.scene);
    deliver_piece(HOST_DATA_CHUNK_MAX, HOST_DATA_FIRST, 0);
    CHECK(state.in_background == probe && state.is_mid_answer && wm.suspended);

    app_delivery_t settings = { .kind = APP_DELIVERY_SETTINGS };
    app_deliver(&state, &wm, probe, &settings, 1);
    CHECK(state.in_background == NULL && !state.is_mid_answer && !wm.suspended);
    deliver_piece(HOST_DATA_CHUNK_MAX, HOST_DATA_LAST, 2);
    CHECK(wm.current == &menu.scene && app_scene_app(probe) == NULL);
    CHECK(wm_pending_change(&wm) == WM_CHANGED_BY_DATA);
    wm_shown(&wm);
    deliver_piece(HOST_DATA_CHUNK_MAX, 0, 2);
    CHECK(app_scene_app(probe) == NULL && wm_pending_change(&wm) == WM_CLEAN);

    static uint8_t bytes[1] = { 7 };
    app_delivery_t explicit_request = { .kind = APP_DELIVERY_PIECE, .request = HOST_REQUEST_FIRST,
                                        .http_status = 200, .data = bytes, .length = sizeof bytes,
                                        .flags = HOST_DATA_FIRST };
    app_deliver(&state, &wm, probe, &explicit_request, 3);
    app_t *taking = app_scene_app(probe);
    CHECK(taking != NULL && state.is_mid_answer);
    app_deliver(&state, &wm, probe, &settings, 4);
    CHECK(app_scene_app(probe) == taking && state.is_mid_answer && wm.suspended);
    explicit_request.flags = HOST_DATA_LAST;
    app_deliver(&state, &wm, probe, &explicit_request, 5);
    CHECK(state.in_background == NULL && !state.is_mid_answer && !wm.suspended);
}

static void test_a_press_in_the_middle_of_an_answer(void)
{
    start_on(&menu.scene);
    deliver_piece(HOST_DATA_CHUNK_MAX, HOST_DATA_FIRST, 0);
    CHECK(wm.suspended && state.in_background == probe);
    /* the user presses something: the scene is back to take it */
    app_abandoned_t abandoned;
    app_delivery_abort(&state, &wm, 0, &abandoned);
    CHECK(!wm.suspended && state.in_background == NULL && !state.is_mid_answer);
    /* whoever asked is told whose answer was cut short */
    CHECK(abandoned.owner == probe && abandoned.request == 1);
    CHECK(wm.current == &menu.scene && app_scene_app(probe) == NULL);
    /* the rest of that answer is nobody's: no new instance is loaded for it */
    deliver_piece(HOST_DATA_CHUNK_MAX, 0, 0);
    CHECK(!wm.suspended && app_scene_app(probe) == NULL);
    deliver_piece(2500 - 2048, HOST_DATA_LAST, 0);
    CHECK(wm.current == &menu.scene && state.in_background == NULL);
    /* and the next answer is taken from its start as ever */
    deliver_piece(HOST_DATA_CHUNK_MAX, HOST_DATA_FIRST, 0);
    CHECK(wm.suspended && state.in_background == probe);
    /* with no answer being taken a press gives way to nothing */
    start_on(&menu.scene);
    app_delivery_abort(&state, &wm, 0, &abandoned);
    CHECK(!wm.suspended && wm.current == &menu.scene && abandoned.owner == NULL);
}

static void test_opened_while_taking_an_answer(void)
{
    start_on(&menu.scene);
    deliver_piece(HOST_DATA_CHUNK_MAX, HOST_DATA_FIRST, 0);
    CHECK(state.in_background == probe);
    /* the user opens it before the answer is whole */
    wm_open(&wm, probe);
    deliver_piece(HOST_DATA_CHUNK_MAX, 0, 0);
    /* it is the scene now: an answer for anybody else must not unload it */
    CHECK(state.in_background == NULL && wm.current == probe);
    CHECK(app_scene_app(probe) != NULL);

    /* BACK leaves an application that does not take it itself: its down
     * and up are the application's, the press is the way out */
    wm_button(&wm, SCENE_BUTTON_BACK, SCENE_PRESS_DOWN);
    wm_button(&wm, SCENE_BUTTON_BACK, SCENE_PRESS_UP);
    CHECK(wm.current == probe);
    wm_button(&wm, SCENE_BUTTON_BACK, SCENE_PRESS_SHORT);
    CHECK(wm.current == &menu.scene && app_scene_app(probe) == NULL);
}

static void test_a_scene_that_is_not_an_application(void)
{
    start_on(&menu.scene);
    CHECK(app_scene_app(&menu.scene) == NULL);
    CHECK(app_scene_begin_background(&menu.scene) == NULL);
    app_scene_end_background(&menu.scene);
    CHECK(app_scene_take_reaction(&menu.scene).kind == SCENE_IGNORED);
}

static void test_scene_state_lifetime(void)
{
    char path[128];
    snprintf(path, sizeof path, "%s/probe/app.wasm", root);
    copy_file("build/probe/app.wasm", path);
    start_on(probe);
    app_button(app_scene_app(probe), 13, BTN_PRESSED);
    wm_suspend(&wm);
    CHECK(app_scene_app(probe) == NULL);
    wm_resume(&wm);

    static uint8_t bits[400 * 300 / 8];
    gfx_t surface;
    gfx_init(&surface, bits, 400, 300);
    app_button(app_scene_app(probe), 14, BTN_PRESSED);
    app_draw(app_scene_app(probe), &surface);
    CHECK(gfx_get(&surface, 2, 61));

    app_button(app_scene_app(probe), 13, BTN_PRESSED);
    wm_close(&wm, probe);
    CHECK(app_scene_app(probe) == NULL);
    wm_open(&wm, probe);
    gfx_clear(&surface, false);
    app_button(app_scene_app(probe), 14, BTN_PRESSED);
    app_draw(app_scene_app(probe), &surface);
    CHECK(!gfx_get(&surface, 2, 61));
    wm_close(&wm, probe);
    copy_file("../../../sdk/abi/3/probe/app.wasm", path);
}

static void count_notification(void *context)
{
    int *count = context;
    (*count)++;
}

static void test_notification_consumption(void)
{
    start_on(&menu.scene);
    int consumed = 0;
    app_delivery_t notification = { .kind = APP_DELIVERY_INSTALLED,
                                    .loaded = count_notification, .loaded_context = &consumed };
    char module[128], unavailable[128];
    snprintf(module, sizeof module, "%s/probe/app.wasm", root);
    snprintf(unavailable, sizeof unavailable, "%s/probe/held.wasm", root);
    CHECK(rename(module, unavailable) == 0);
    app_deliver(&state, &wm, probe, &notification, 0);
    CHECK(consumed == 0);
    CHECK(rename(unavailable, module) == 0);
    app_deliver(&state, &wm, probe, &notification, 0);
    CHECK(consumed == 1);
    /* A background trap must restore the foreground instead of opening an error scene. */
    notification.kind = APP_DELIVERY_UPDATED;
    app_deliver(&state, &wm, probe, &notification, 0);
    CHECK(consumed == 2 && app_scene_app(probe) == NULL);
    CHECK(wm.current == &menu.scene && !wm.suspended);
    consumed = 1;
    /* Update's handler traps. The load hook has already consumed it. */
    start_on(probe);
    notification.kind = APP_DELIVERY_UPDATED;
    app_deliver(&state, &wm, probe, &notification, 0);
    CHECK(consumed == 2);
    CHECK(app_scene_app(probe) == NULL || app_has_stopped(app_scene_app(probe)));
    probe->ops->leave(probe);
    /* A stopped on-screen instance is loaded too. In particular, a trap
     * in app_on_load must not leave the notification pending forever. */
    start_on(probe);
    app_t *app = app_scene_app(probe);
    CHECK(app != NULL);
    app_button(app, 4, BTN_PRESSED);
    CHECK(app_has_stopped(app));
    notification.kind = APP_DELIVERY_INSTALLED;
    app_deliver(&state, &wm, probe, &notification, 0);
    CHECK(consumed == 3);
    probe->ops->leave(probe);
}

static int32_t frozen_file_read(const char *name, const char *file, int32_t offset,
                                 void *bytes, int32_t capacity)
{
    (void)name;
    char path[256];
    snprintf(path, sizeof path, "../../../sdk/abi/3/settings/%s", file);
    FILE *input = fopen(path, "rb");
    if (input == NULL) {
        return HOST_ERR_NOT_FOUND;
    }
    CHECK(fseek(input, offset, SEEK_SET) == 0);
    size_t length = fread(bytes, 1, (size_t)capacity, input);
    fclose(input);
    return (int32_t)length;
}

static int32_t frozen_file_size(const char *name, const char *file)
{
    (void)name;
    char path[256];
    snprintf(path, sizeof path, "../../../sdk/abi/3/settings/%s", file);
    struct stat info;
    return stat(path, &info) == 0 ? (int32_t)info.st_size : HOST_ERR_NOT_FOUND;
}

static const char *fixture_language;
static const char *frozen_language(void) { return fixture_language; }

static int32_t frozen_setting_get(const char *key, char *value, int32_t capacity)
{
    if (strcmp(key, SYS_KEY_LANGUAGE) != 0) {
        return HOST_ERR_NOT_FOUND;
    }
    if (capacity < 2) {
        return HOST_ERR_NO_SPACE;
    }
    memcpy(value, fixture_language, 2);
    return 2;
}

static void test_frozen_settings_translation(void)
{
    start_on(&menu.scene);
    char *text = app_store_read_manifest("../../../sdk/abi/3", "settings");
    app_manifest_t manifest;
    CHECK(text != NULL && app_manifest_parse(text, &manifest) == APP_MANIFEST_OK);
    free(text);
    CHECK(manifest.abi == 3 && manifest.system);
    app_services_t services = SERVICES;
    services.language = frozen_language;
    services.file_read = frozen_file_read;
    services.file_size = frozen_file_size;
    services.system_setting_get = frozen_setting_get;
    scene_t *settings = app_scene_create(&manifest, "../../../sdk/abi/3", &services);
    CHECK(settings != NULL);
    static uint8_t english[(400 * 260 / 8)], russian[(400 * 260 / 8)];
    gfx_t surface;
    fixture_language = "en";
    CHECK(app_scene_begin_background(settings) != NULL);
    gfx_init(&surface, english, 400, 260);
    gfx_clear(&surface, false);
    app_draw(app_scene_app(settings), &surface);
    CHECK(!app_has_stopped(app_scene_app(settings)));
    app_scene_end_background(settings);
    fixture_language = "ru";
    CHECK(app_scene_begin_background(settings) != NULL);
    gfx_init(&surface, russian, 400, 260);
    gfx_clear(&surface, false);
    app_draw(app_scene_app(settings), &surface);
    CHECK(!app_has_stopped(app_scene_app(settings)));
    CHECK(memcmp(english, russian, sizeof english) != 0);
    app_scene_end_background(settings);
    app_scene_destroy(settings);
}

static char auth_cookie[32];
static int auth_checked;
static int32_t auth_get(const char *app, const char *key, void *value, int32_t capacity)
{
    if (strcmp(app, "probe") == 0 && strcmp(key, "auth_cookie") == 0 && auth_cookie[0] && capacity >= 7) {
        memcpy(value, auth_cookie, 7);
        return 7;
    }
    return HOST_ERR_NOT_FOUND;
}
static int32_t auth_set(const char *app, const char *key, const void *value, int32_t length)
{
    CHECK(strcmp(app, "probe") == 0);
    if (strcmp(key, "auth_cookie") == 0 && length == 7) { memcpy(auth_cookie, value, 7); }
    if (strcmp(key, "auth_checked") == 0 && length == 7 && memcmp(value, "sid=one", 7) == 0) {
        auth_checked++;
    }
    return HOST_OK;
}
static const char *current_firmware(void) { return HOST_ABI_3_FIRMWARE_FIRST; }
static void test_headers_across_background_unloads(void)
{
    start_on(&menu.scene);
    char *text = app_store_read_manifest("build", "probe");
    app_manifest_t manifest;
    CHECK(text != NULL && app_manifest_parse(text, &manifest) == APP_MANIFEST_OK);
    free(text);
    app_services_t services = SERVICES;
    services.firmware_version = current_firmware;
    services.kv_get = auth_get;
    services.kv_set = auth_set;
    scene_t *owner = app_scene_create(&manifest, "build", &services);
    CHECK(owner != NULL);
    answer_headers_t headers;
    answer_headers_reset(&headers);
    answer_headers_add(&headers, "Set-Cookie", "sid=one");
    app_delivery_t delivery = { .kind = APP_DELIVERY_PIECE, .request = 31, .http_status = 401,
        .headers = &headers, .flags = HOST_DATA_FIRST | HOST_DATA_LAST };
    app_deliver(&state, &wm, owner, &delivery, 0);
    CHECK(app_scene_app(owner) == NULL && wm.current == &menu.scene);
    CHECK(memcmp(auth_cookie, "sid=one", 7) == 0);
    /* Another owner never sees the completed response's headers. */
    delivery.request = 30;
    delivery.headers = NULL;
    app_deliver(&state, &wm, probe, &delivery, 1);
    CHECK(wm.current == &menu.scene && app_scene_app(probe) == NULL);
    delivery.request = 32;
    delivery.http_status = 200;
    app_deliver(&state, &wm, owner, &delivery, 2);
    CHECK(auth_checked == 1);
    CHECK(app_scene_app(owner) == NULL && wm.current == &menu.scene);
    app_scene_destroy(owner);
}

static void test_scene_inversion_lifetime(void)
{
    start_on(&menu.scene);
    char *text = app_store_read_manifest("build", "probe");
    app_manifest_t manifest;
    CHECK(text != NULL && app_manifest_parse(text, &manifest) == APP_MANIFEST_OK);
    free(text);
    app_services_t services = SERVICES;
    services.firmware_version = current_firmware;
    scene_t *owner = app_scene_create(&manifest, "build", &services);
    CHECK(owner != NULL);
    wm_open(&wm, owner);
    app_button(app_scene_app(owner), 100, BTN_PRESSED);
    app_scene_take_reaction(owner);
    CHECK(owner->inverted && !menu.scene.inverted);
    menu_t overlay = { .scene = { .ops = &MENU_OPS } };
    wm_open(&wm, &overlay.scene);
    CHECK(wm.current == &overlay.scene && !wm.current->inverted);
    app_delivery_t delivery = { .kind = APP_DELIVERY_PIECE, .request = 40, .http_status = 200,
        .flags = HOST_DATA_FIRST | HOST_DATA_LAST };
    app_deliver(&state, &wm, owner, &delivery, 0);
    CHECK(wm.current == &overlay.scene && !wm.current->inverted);
    CHECK(owner->inverted);
    wm_open(&wm, owner);
    CHECK(owner->inverted && app_screen_inverted(app_scene_app(owner)));
    wm_close(&wm, owner);
    CHECK(!owner->inverted);
    wm_open(&wm, owner);
    CHECK(!owner->inverted && !app_screen_inverted(app_scene_app(owner)));
    wm_close(&wm, owner);
    app_scene_destroy(owner);
}

static int settings_actions;
static const char *settings_firmware(void) { return HOST_ABI_3_FIRMWARE_FIRST; }
static int32_t record_settings_action(int action)
{
    CHECK(action == SYS_DO_RESTART);
    settings_actions++;
    return HOST_OK;
}

static void test_question_survives_background_delivery(void)
{
    start_on(&menu.scene);
    char *text = app_store_read_manifest("build", "settings");
    app_manifest_t manifest;
    CHECK(text != NULL && app_manifest_parse(text, &manifest) == APP_MANIFEST_OK);
    free(text);
    app_services_t services = SERVICES;
    services.firmware_version = settings_firmware;
    services.system_do = record_settings_action;
    scene_t *settings = app_scene_create(&manifest, "build", &services);
    CHECK(settings != NULL);
    wm_open(&wm, settings);
    /* Restart is the sixth row of Settings. */
    for (int row = 0; row < 5; row++) {
        wm_button(&wm, SCENE_BUTTON_DOWN, SCENE_PRESS_SHORT);
    }
    wm_button(&wm, SCENE_BUTTON_OK, SCENE_PRESS_SHORT);
    static uint8_t bits[400 * 300 / 8];
    gfx_t surface;
    gfx_init(&surface, bits, 400, 300);
    wm_draw(&wm, &surface);
    CHECK(settings->question != 0);
    uint32_t restart_question = settings->question;
    question_press_t presses = {0};
    question_press_shown(&presses, settings, 100);
    CHECK(!question_press_accepts(&presses, settings, SCENE_BUTTON_OK, SCENE_PRESS_DOWN, 101));

    deliver_values(102);
    CHECK(wm.current == settings && !wm.suspended);
    wm_draw(&wm, &surface);
    CHECK(settings->question == restart_question);
    question_press_shown(&presses, settings, 103);
    /* A second restore is consumed before KEYUP even if no frame was drawn. */
    deliver_values(104);
    CHECK(question_press_accepts(&presses, settings, SCENE_BUTTON_OK, SCENE_PRESS_UP, 105));
    wm_button(&wm, SCENE_BUTTON_OK, SCENE_PRESS_UP);
    CHECK(settings->question == restart_question);
    CHECK(question_press_accepts(&presses, settings, SCENE_BUTTON_OK, SCENE_PRESS_SHORT, 105));
    wm_button(&wm, SCENE_BUTTON_OK, SCENE_PRESS_SHORT);
    CHECK(settings_actions == 1 && settings->question == 0);

    wm_close(&wm, settings);
    wm_open(&wm, settings);
    for (int row = 0; row < 5; row++) {
        wm_button(&wm, SCENE_BUTTON_DOWN, SCENE_PRESS_SHORT);
    }
    wm_button(&wm, SCENE_BUTTON_OK, SCENE_PRESS_SHORT);
    CHECK(settings->question != 0 && settings->question != restart_question);
    uint32_t reopened_question = settings->question;
    wm_draw(&wm, &surface);
    question_press_shown(&presses, settings, 106);
    CHECK(!question_press_accepts(&presses, settings, SCENE_BUTTON_OK, SCENE_PRESS_DOWN, 107));
    wm_button(&wm, SCENE_BUTTON_BACK, SCENE_PRESS_SHORT);
    CHECK(settings->question == 0);
    wm_button(&wm, SCENE_BUTTON_DOWN, SCENE_PRESS_SHORT);
    wm_button(&wm, SCENE_BUTTON_OK, SCENE_PRESS_SHORT);
    CHECK(settings->question != 0 && settings->question != reopened_question);
    CHECK(!question_press_accepts(&presses, settings, SCENE_BUTTON_OK, SCENE_PRESS_SHORT, 108));
    CHECK(settings_actions == 1);
    wm_close(&wm, settings);
    app_scene_destroy(settings);
}

static void test_background_raw_header_probe(void)
{
    const int requests[] = { 1, HOST_REQUEST_FIRST };
    const int64_t first_piece_ms = 5 * 60 * 1000;
    for (unsigned index = 0; index < sizeof requests / sizeof requests[0]; index++) {
        start_on(&menu.scene);
        host_answer_info_t response = { .status = 403 };
        app_delivery_t headers = { .kind = APP_DELIVERY_PIECE, .request = requests[index],
            .http_status = 403, .response = &response, .flags = HOST_DATA_FIRST,
            .headers_only = true };
        app_abandoned_t result = app_deliver(&state, &wm, probe, &headers, 0);
        CHECK(result.owner == NULL && !result.rest_unwanted);
        CHECK(wm.current == &menu.scene && !wm.suspended);
        CHECK(state.in_background == NULL && !state.is_mid_answer);
        CHECK(state.answer_owner == NULL && state.answer_instance == 0);
        CHECK(app_scene_loaded_app(probe) == NULL);
        CHECK(menu.left == 1 && menu.entered == 2);

        app_delivery_t alarm = { .kind = APP_DELIVERY_ALARM, .request = 0 };
        result = app_deliver(&state, &wm, probe, &alarm, 1);
        CHECK(result.owner == NULL && !wm.suspended);
        CHECK(state.answer_owner == NULL && state.answer_instance == 0);

        uint8_t byte = 3;
        app_delivery_t piece = { .kind = APP_DELIVERY_PIECE, .request = requests[index],
            .http_status = 403, .response = &response, .data = &byte, .length = 1,
            .flags = HOST_DATA_FIRST };
        result = app_deliver(&state, &wm, probe, &piece, first_piece_ms);
        CHECK(result.owner == NULL && !result.rest_unwanted);
        CHECK(wm.suspended && state.in_background == probe && state.is_mid_answer);
        CHECK(state.answer_owner == probe && state.answer_instance == app_scene_instance(probe));
        CHECK(state.answer_instance != 0);
        piece.flags = HOST_DATA_LAST;
        result = app_deliver(&state, &wm, probe, &piece, first_piece_ms + 1);
        CHECK(result.owner == NULL && !wm.suspended);
        CHECK(state.in_background == NULL && !state.is_mid_answer);
        CHECK(state.answer_owner == NULL && state.answer_instance == 0);
    }
}

static void test_host_document_rejection(void)
{
    start_on(&menu.scene);
    app_manifest_t manifest = { .name = "document", .title = "Document", .abi = HOST_API_VERSION };
    char directory[128], path[160];
    snprintf(directory, sizeof directory, "%s/document", root);
    CHECK(mkdir(directory, 0755) == 0);
    snprintf(path, sizeof path, "%s/app.wasm", directory);
    copy_file("build/document/app.wasm", path);
    document_scene = app_scene_create(&manifest, root, &SERVICES);
    CHECK(document_scene != NULL);
    host_answer_info_t response = { .content_type = "application/json", .status = 403 };
    app_delivery_t headers = { .kind = APP_DELIVERY_PIECE, .request = 0, .http_status = 403,
        .response = &response, .flags = HOST_DATA_FIRST, .headers_only = true };
    /* The real host propagates document-only rejection in foreground and background. */
    wm_open(&wm, document_scene);
    CHECK(!host_deliver("document", &headers));
    app_delivery_t closing = { .kind = APP_DELIVERY_PIECE, .request = 0,
        .http_status = HOST_FAILED_NO_ANSWER, .flags = HOST_DATA_FAILED, .is_closing = true };
    CHECK(!host_deliver("document", &closing));
    wm_open_from_home(&wm, &menu.scene);
    CHECK(!host_deliver("document", &headers));
    CHECK(wm.suspended && state.in_background == document_scene);
    CHECK(!host_deliver("document", &closing));
    CHECK(!wm.suspended && state.in_background == NULL);
    CHECK(app_scene_loaded_app(document_scene) == NULL);
    CHECK(!host_deliver("missing", &headers));
    app_scene_destroy(document_scene);
    document_scene = NULL;
}

int main(void)
{
    install_probe();
    test_background_raw_header_probe();
    test_host_document_rejection();
    test_manifest_ownership();
    test_missing_application_area();
    test_frozen_settings_translation();
    test_notification_consumption();
    test_on_the_screen();
    test_foreground_stream_interrupted_by_another_owner();
    test_foreground_stream_interrupted_by_navigation();
    test_closing_failure_reaches_only_its_answer();
    test_stream_owner_cannot_load();
    test_background_load_memory_failure();
    test_failure_starts_a_different_answer();
    test_cut_poll_settings_allow_a_failed_refetch();
    test_settings_allow_a_failed_refetch();
    test_off_the_screen();
    test_asks_for_the_screen();
    test_not_twice_in_a_row();
    test_not_from_under_the_user();
    test_a_request_that_broke_off();
    test_something_else_in_the_middle_of_an_answer();
    test_settings_discard_an_unfinished_poll();
    test_settings_document_registration();
    test_a_press_in_the_middle_of_an_answer();
    test_opened_while_taking_an_answer();
    test_a_scene_that_is_not_an_application();
    test_scene_state_lifetime();
    test_headers_across_background_unloads();
    test_scene_inversion_lifetime();
    test_question_survives_background_delivery();
    REPORT();
}
