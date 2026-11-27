/* SPDX-License-Identifier: MIT */
#include <stdbool.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <errno.h>
#include "app_manifest.h"

static bool refuse_job_allocation, refuse_body_template;
static bool refuse_authorization;
static void (*before_read_line)(void);

static bool test_allows_url(const char *manifest, const char *url,
                            app_manifest_value_fn value_of, void *context)
{
    if (refuse_authorization) {
        errno = ENOMEM;
        return false;
    }
    return app_manifest_allows_url(manifest, url, value_of, context);
}

static void *test_calloc(size_t count, size_t size)
{
    if (refuse_job_allocation) {
        refuse_job_allocation = false;
        return NULL;
    }
    return calloc(count, size);
}

static char *test_strdup(const char *text)
{
    if (refuse_body_template) {
        refuse_body_template = false;
        return NULL;
    }
    return strdup(text);
}

static ssize_t test_getline(char **line, size_t *capacity, FILE *stream)
{
    if (before_read_line != NULL) before_read_line();
    return getline(line, capacity, stream);
}

static bool fixed_wall_clock;
static time_t wall_clock;

/* Range boundaries must stay exact even if the test is descheduled for a second. */
static time_t test_wall_time(time_t *out)
{
    time_t now = fixed_wall_clock ? wall_clock : time(NULL);
    if (out != NULL) {
        *out = now;
    }
    return now;
}

#define main simulator_main
#define time test_wall_time
#define calloc test_calloc
#define strdup test_strdup
#define getline test_getline
#define app_manifest_allows_url test_allows_url
#include "sim.c"
#undef app_manifest_allows_url
#undef getline
#undef strdup
#undef calloc
#undef time
#undef main
#include <assert.h>

static json_t *read_request(FILE *requests)
{
    char *line = NULL;
    size_t size = 0;
    assert(getline(&line, &size, requests) > 0);
    json_t *request = json_parse(line);
    free(line);
    assert(request != NULL);
    return request;
}

static void reply(FILE *answers, const char *text)
{
    assert(fprintf(answers, "%s\n", text) > 0);
    assert(fflush(answers) == 0);
}

static void set_value(const char *key, const char *value)
{
    assert(kv_store(s_options.app, key, value, (int32_t)strlen(value), true) == HOST_OK);
}

static void *fetch_on_worker(void *context)
{
    fetch(context);
    return NULL;
}

static void test_network_icon(void)
{
    job_t job = { .is_poll = true, .wants_values = true, .item = -1 };
    const int server_failures[] = { HOST_FAILED_NO_ANSWER, HOST_FAILED_TIMEOUT,
                                    HOST_FAILED_NOT_TRUSTED, HOST_FAILED_NOT_READY,
                                    HOST_FAILED_NO_MEMORY, 401, 503 };
    for (size_t index = 0; index < sizeof server_failures / sizeof server_failures[0]; index++) {
        job.status = server_failures[index];
        hand_over(&job);
        assert(s_network_connected);
    }
    job.status = HOST_FAILED_NO_NETWORK;
    hand_over(&job);
    assert(!s_network_connected);
    job.status = HOST_FAILED_NO_ANSWER;
    hand_over(&job);
    assert(!s_network_connected);
    job.status = 503;
    hand_over(&job);
    assert(s_network_connected);
}

static void test_mocks(const char *state)
{
    char path[PATH_BYTES];
    snprintf(path, sizeof path, "%s/mocks.json", state);
    s_options.mocks = path;
    assert(!load_mocks());
    assert(strstr(s_log, "cannot be read") != NULL);
    const char *invalid[] = { "", "{", "[]", "null", "{} trailing", "{\"http://example.test\":null}",
        "{\"http://example.test\":{\"file\":\"missing-body.json\"}}",
        "{\"http://example.test\":{\"file\":5}}" };
    for (size_t index = 0; index < sizeof invalid / sizeof invalid[0]; index++) {
        FILE *file = fopen(path, "wb");
        assert(file != NULL);
        assert(fputs(invalid[index], file) >= 0);
        assert(fclose(file) == 0);
        assert(!load_mocks());
        assert(s_mocks == NULL);
    }
    FILE *file = fopen(path, "wb");
    assert(file != NULL);
    static const char nul[] = "{}\0trailing";
    assert(fwrite(nul, 1, sizeof nul - 1, file) == sizeof nul - 1);
    assert(fclose(file) == 0);
    assert(!load_mocks());
    assert(remove(path) == 0);

    /* A supplied fetcher must receive no bytes, even for an empty set of mocks. */
    s_fetcher_in = tmpfile();
    assert(s_fetcher_in != NULL);
    const char *sets[] = { "{}", "{\"http://example.test/other\":{}}",
                          "{\"http://example.test/data\":{\"method\":\"POST\"}}",
                          "{\"http://example.test/data\":{\"request_body\":\"different\"}}" };
    for (size_t index = 0; index < sizeof sets / sizeof sets[0]; index++) {
        file = fopen(path, "wb");
        assert(file != NULL);
        assert(fputs(sets[index], file) >= 0);
        assert(fclose(file) == 0);
        assert(load_mocks());
        job_t job = { .method = HOST_HTTP_GET, .is_poll = true, .item = -1 };
        snprintf(job.url, sizeof job.url, "http://example.test/data");
        /* The worker must not touch the main thread's log, even when nearly full. */
        memset(s_log, 'x', LOG_KEPT_BYTES - 1);
        s_log[LOG_KEPT_BYTES - 1] = '\0';
        s_log_length = LOG_KEPT_BYTES - 1;
        uint64_t written_before = s_log_written;
        pthread_t worker;
        assert(pthread_create(&worker, NULL, fetch_on_worker, &job) == 0);
        assert(pthread_join(worker, NULL) == 0);
        assert(s_log_written == written_before);
        assert(s_log_length == LOG_KEPT_BYTES - 1);
        assert(strspn(s_log, "x") == s_log_length);
        assert(job.status == HOST_FAILED_NO_ANSWER && job.response.length == -1);
        assert(ftell(s_fetcher_in) == 0);
        hand_over(&job);
        assert(strstr(s_log, "no mock matches GET http://example.test/data; network disabled by --mocks") != NULL);
        json_free(s_mocks);
        s_mocks = NULL;
    }
    assert(fclose(s_fetcher_in) == 0);
    s_fetcher_in = NULL;
    assert(remove(path) == 0);
    s_options.mocks = NULL;
    assert(load_mocks());
}

static void test_time_and_recurrence(void)
{
    fixed_wall_clock = true;
    wall_clock = (time_t)1800000000;
    const int64_t moments[] = { HOST_TIME_MIN, -1, HOST_CLOCK_MIN - 1, HOST_CLOCK_MIN,
                                HOST_CLOCK_MAX, HOST_CLOCK_MAX + 1 };
    setenv("TZ", "FIX-1", 1);
    tzset();
    uint8_t pixels[SCREEN_WIDTH * WM_HEADER_HEIGHT / 8];
    gfx_t surface;
    gfx_init(&surface, pixels, SCREEN_WIDTH, WM_HEADER_HEIGHT);
    scene_t scene = { 0 };
    for (size_t index = 0; index < sizeof moments / sizeof moments[0]; index++) {
        s_options.at = moments[index];
        s_clock_offset_s = moments[index] - wall_clock;
        s_forward_ms = 0;
        bool known = moments[index] >= HOST_CLOCK_MIN && moments[index] <= HOST_CLOCK_MAX;
        assert(services_unix_time() == (known ? moments[index] : 0));
        assert(services_offset() == (known ? 3600 : 0));
        gfx_clear(&surface, false);
        draw_header(&surface, &scene, NULL);
        int date_ink = 0;
        for (int y = 0; y < WM_HEADER_HEIGHT - 1; y++) {
            for (int x = SCREEN_WIDTH / 2; x < SCREEN_WIDTH; x++) {
                date_ink += gfx_get(&surface, x, y);
            }
        }
        assert((date_ink > 0) == known);
    }
    assert(offset_at(HOST_TIME_MIN) == 3600);
    assert(offset_at(0) == 3600);
    assert(offset_at(HOST_TIME_MAX) == 3600);
    assert(offset_at(HOST_TIME_MIN - 1) == 0);
    assert(offset_at(HOST_TIME_MAX + 1) == 0);
    s_settings.time_auto = 0;
    assert(services_time_set(HOST_CLOCK_MIN - 1) == HOST_ERR_ARGUMENT);
    assert(services_time_set(HOST_CLOCK_MAX + 1) == HOST_ERR_ARGUMENT);
    assert(services_time_set(HOST_CLOCK_MIN) == HOST_OK);
    assert(services_time_set(HOST_CLOCK_MAX) == HOST_OK);
    const int64_t corrections[] = { 30, -30, 86400, -86400 };
    for (size_t index = 0; index < sizeof corrections / sizeof corrections[0]; index++) {
        deadline_table_init(&s_deadlines);
        s_forward_ms = 0;
        assert(services_time_set(1800000000) == HOST_OK);
        assert(services_alarm_every("test", 0, 60, 15));
        int64_t first_due = s_alarm_due_uptime_ms[0];
        assert(services_time_set(1800000000 + corrections[index]) == HOST_OK);
        assert(s_alarm_due_uptime_ms[0] == first_due);
        s_forward_ms += 20000;
        deadline_id_t alarm = { .kind = DUE_ALARM, .number = 0 };
        answer_due(&alarm);
        assert(s_alarm_due_uptime_ms[0] == first_due + 60000);
        s_forward_ms += 55000;
        answer_due(&alarm);
        assert(s_alarm_due_uptime_ms[0] == first_due + 120000);
        s_forward_ms += 180000;
        int64_t before = services_uptime();
        answer_due(&alarm);
        assert(s_alarm_due_uptime_ms[0] >= before + 60000);
        assert(s_alarm_due_uptime_ms[0] <= services_uptime() + 60000);
        assert(services_alarm("test", 0, 0, false));
        assert(s_alarm_every_s[0] == 0);
    }
    deadline_table_init(&s_deadlines);
    s_options.at = 0;
    s_forward_ms = 0;
    fixed_wall_clock = false;
    s_clock_offset_s = 0;
}

static void test_preparation_memory_failure(void)
{
    s_manifest_json = strdup("{\"hosts\":[\"example.test\"]}");
    job_t *request = calloc(1, sizeof *request);
    assert(request != NULL);
    snprintf(request->url, sizeof request->url, "https://example.test/file");
    refuse_authorization = true;
    prepare_job(request);
    refuse_authorization = false;
    assert(request->status == HOST_FAILED_NO_MEMORY);
    free_job(request);
    free(s_manifest_json);
    s_manifest_json = NULL;
    for (int is_poll = 0; is_poll <= 1; is_poll++) {
        job_t *job = calloc(1, sizeof *job);
        assert(job != NULL);
        job->is_poll = is_poll;
        job->status = HOST_FAILED_NO_MEMORY;
        ask(job);
        dispatch_job();
        assert(s_jobs_asked == job && job->only_on_screen == (bool)is_poll);
        s_jobs_asked = NULL;
        s_job_active = false;
        free_job(job);
    }
}

static void test_empty_and_long_list(void)
{
    s_manifest_json = strdup("{\"polls\":[{\"url\":\"http://example.test/fixed\",\"each\":\"items\",\"every\":60},"
        "{\"url\":\"http://example.test/{{items}}\",\"each\":\"items\",\"prefix\":\"x:\",\"every\":60}],"
        "\"settings\":[{\"key\":\"items\",\"title\":\"Items\",\"type\":\"list\"}]}");
    int items = 0;
    for (int empty = 0; empty < 2; empty++) {
        if (empty) set_value("items", "");
        job_t *job = start_poll(0, 0, &items);
        assert(job != NULL);
        prepare_job(job);
        assert(job->status == 0 && strcmp(job->url, "http://example.test/fixed") == 0);
        free_job(job);
        refuse_job_allocation = true;
        assert(start_poll(0, 0, &items) == NULL && !refuse_job_allocation);
        refuse_body_template = true;
        job = start_poll(0, 0, &items);
        assert(job != NULL && !refuse_body_template);
        prepare_job(job);
        assert(job->status == HOST_FAILED_NO_MEMORY);
        free_job(job);
    }
    char list[128];
    memset(list, 'a', sizeof list);
    list[0] = 'x'; list[1] = ':'; list[sizeof list - 1] = '\0';
    set_value("items", list);
    job_t *job = start_poll(1, 0, &items);
    assert(job != NULL && items == 1);
    prepare_job(job);
    assert(job->status == 0 && strlen(job->each_value) == APP_LIST_ITEM_MAX - 2);
    assert(strlen(job->url) == strlen("http://example.test/") + APP_LIST_ITEM_MAX - 2);
    free_job(job);
    char path[PATH_BYTES];
    store_path(path, "probe", "items");
    assert(remove(path) == 0);
    free(s_manifest_json);
    s_manifest_json = NULL;
}

static void test_content_lengths(void)
{
    s_fetcher_in = tmpfile();
    s_fetcher_out = tmpfile();
    assert(s_fetcher_in != NULL && s_fetcher_out != NULL);
    const char *lengths[] = { "0", "2147483647", "2147483648", "4294967296", "9223372036854775807",
        "00000000000000000000000000000000123", "123junk", "-1" };
    for (size_t index = 0; index < sizeof lengths / sizeof lengths[0]; index++) {
        assert(fprintf(s_fetcher_out, "{\"status\":200,\"headers\":{\"content-length\":\"%s\"},\"body\":\"\"}\n", lengths[index]) > 0);
    }
    rewind(s_fetcher_out);
    for (size_t index = 0; index < sizeof lengths / sizeof lengths[0]; index++) {
        job_t job = { .method = HOST_HTTP_HEAD, .number = INT32_MAX };
        snprintf(job.url, sizeof job.url, "http://example.test/length");
        char next[APP_URL_MAX + 1];
        exchange(&job, next, sizeof next);
        assert(job.status == 200);
        int32_t expected = index == 0 ? 0 : index == 1 ? INT32_MAX : index == 5 ? 123 : -1;
        assert(job.response.length == expected);
        free(job.answer);
        char mock_text[256];
        snprintf(mock_text, sizeof mock_text, "{\"headers\":{\"CONTENT-LENGTH\":\"%s\"},\"body\":\"\"}", lengths[index]);
        json_t *mock = json_parse(mock_text);
        job = (job_t){ .method = HOST_HTTP_HEAD };
        answer_from_mock(&job, mock);
        assert(job.response.length == expected);
        free(job.answer);
        json_free(mock);
    }
    json_t *mock = json_parse("{\"body\":\"abc\"}");
    job_t absent = {0};
    answer_from_mock(&absent, mock);
    assert(absent.response.length == -1);
    free(absent.answer); json_free(mock);
    fclose(s_fetcher_in);
    fclose(s_fetcher_out);
    s_fetcher_in = s_fetcher_out = NULL;
}

static int timestamp_reads;
static bool forward_during_body;

static void change_clock_during_body(void)
{
    if (++timestamp_reads == 2) {
        if (forward_during_body) s_forward_ms += 3600000;
        else s_clock_offset_s -= 10000;
    }
}

static void test_header_timestamps(void)
{
    for (int forward = 0; forward < 2; forward++) {
        fixed_wall_clock = true;
        wall_clock = 1795780800;
        s_clock_offset_s = s_forward_ms = 0;
        forward_during_body = forward;
        timestamp_reads = 0;
        s_fetcher_in = tmpfile(); s_fetcher_out = tmpfile();
        assert(s_fetcher_in != NULL && s_fetcher_out != NULL);
        reply(s_fetcher_out, "{\"event\":\"headers\"}");
        reply(s_fetcher_out, "{\"status\":200,\"headers\":{\"Retry-After\":\"Fri, 27 Nov 2026 12:05:00 GMT\"},\"body\":\"\"}");
        rewind(s_fetcher_out);
        job_t job = { .is_poll = true, .item = -1 };
        snprintf(job.url, sizeof job.url, "http://example.test/slow");
        int64_t before = monotonic_ms();
        char next[APP_URL_MAX + 1];
        before_read_line = change_clock_during_body;
        exchange(&job, next, sizeof next);
        before_read_line = NULL;
        assert(job.status == 200 && job.response.retry_after == 300);
        assert(job.response_at_ms >= before && job.response_at_ms < before + 1000);
        s_poll_cooldown_until_ms = 0;
        hand_over(&job);
        assert(forward ? s_poll_cooldown_until_ms <= monotonic_ms() : s_poll_cooldown_until_ms > monotonic_ms());
        free(job.answer);
        fclose(s_fetcher_in); fclose(s_fetcher_out);
        s_fetcher_in = s_fetcher_out = NULL;
    }
    fixed_wall_clock = false;
    s_clock_offset_s = s_forward_ms = s_poll_cooldown_until_ms = 0;
}

static void take_poll_deadlines(void)
{
    deadline_id_t due;
    while (deadline_table_take_due(&s_deadlines, monotonic_ms(), &due)) {
        assert(due.kind == DUE_POLL);
        answer_due(&due);
    }
}

static void test_poll_scheduling(void)
{
    free(s_manifest_json);
    s_manifest_json = strdup("{\"name\":\"probe\",\"title\":\"Probe\",\"abi\":3,\"polls\":["
        "{\"url\":\"https://example.test/manual\",\"manual\":true,\"pick\":[\"value\"]},"
        "{\"url\":\"https://example.test/list/{{items}}\",\"manual\":true,\"each\":\"items\"}],"
        "\"settings\":[{\"key\":\"items\",\"type\":\"list\",\"title\":\"Items\",\"default\":[\"a\",\"b\",\"c\"]}]}");
    assert(app_manifest_parse(s_manifest_json, &s_manifest) == APP_MANIFEST_OK);
    poll_schedule_init(&s_poll_schedule);
    s_poll_rounds = (poll_rounds_t){0};
    deadline_table_init(&s_deadlines);
    s_poll_cooldown_until_ms = 0;
    s_poll_run_accepted_ms = monotonic_ms() - 1000;
    for (int poll = 0; poll < s_manifest.poll_count; poll++) {
        uint32_t first_ms;
        assert(poll_schedule_add(&s_poll_schedule, SIM_APP_ID, poll, 0, true, &first_ms));
    }
    poll_schedule_want_of(&s_poll_schedule, SIM_APP_ID);
    poll_schedule_network_is_back(&s_poll_schedule);
    assert(next_poll_job() == NULL);
    assert(services_poll_interval("probe", 0, 60) == HOST_ERR_ARGUMENT);
    assert(services_poll_now("probe", 0));
    assert(services_poll_now("probe", 0));
    job_t *job = next_poll_job();
    assert(job != NULL && job->number == 0 && job->wants_values);
    assert(services_poll_now("probe", 0) && services_poll_now("probe", 0));
    job->status = 200;
    finish_poll(job);
    free_job(job);
    assert(next_poll_job() == NULL);
    s_forward_ms += 1001;
    take_poll_deadlines();
    job = next_poll_job();
    assert(job != NULL && job->number == 0);
    s_options.at = 1795780800;
    atomic_store(&s_clock_offset_s, s_options.at - (int64_t)time(NULL));
    s_forward_ms = 0;
    json_t *mock = json_parse("{\"status\":429,\"body\":\"{}\",\"headers\":{"
        "\"Content-Type\":\"application/json\",\"Retry-After\":\"Fri, 27 Nov 2026 12:02:00 GMT\"}}");
    answer_from_mock(job, mock);
    json_free(mock);
    hand_over(job);
    assert(job->response.retry_after == 120);
    finish_poll(job);
    free_job(job);
    assert(next_poll_job() == NULL); /* manual 429 does not invent another run */
    s_poll_run_accepted_ms = monotonic_ms() - 1000;
    assert(services_poll_now("probe", 1));
    assert(next_poll_job() == NULL); /* requested sibling is deferred */
    poll_schedule_want_of(&s_poll_schedule, SIM_APP_ID);
    poll_schedule_network_is_back(&s_poll_schedule);
    assert(next_poll_job() == NULL);
    s_forward_ms += 119000;
    take_poll_deadlines();
    assert(next_poll_job() == NULL);
    s_forward_ms += 1001;
    take_poll_deadlines();
    job = next_poll_job();
    assert(job != NULL && job->number == 1 && job->item == 0 && job->poll_items == 3);
    job->status = 200;
    finish_poll(job);
    free_job(job);
    job = next_poll_job();
    assert(job != NULL && job->item == 1);
    job->status = 429;
    job->response.status = 429;
    job->response_at_ms = monotonic_ms();
    hand_over(job); /* bare 429: ends the round and starts a 60-second cooldown */
    finish_poll(job);
    free_job(job);
    assert(next_poll_job() == NULL && s_poll_cooldown_until_ms > monotonic_ms());
    s_forward_ms += 61000;
    take_poll_deadlines();
    assert(next_poll_job() == NULL);
    assert(services_poll_now("probe", 1));
    job = next_poll_job();
    assert(job != NULL && job->item == 0);
    job->status = HOST_FAILED_NO_NETWORK;
    finish_poll(job);
    free_job(job);
    poll_schedule_network_is_back(&s_poll_schedule);
    job = next_poll_job();
    assert(job != NULL && job->item == 0); /* retries explicitly requested transport failure */
    job->status = 200;
    job->poll_items = 0;
    finish_poll(job);
    free_job(job);
    poll_schedule_network_is_back(&s_poll_schedule);
    assert(next_poll_job() == NULL);
    s_poll_cooldown_until_ms = 0;
    job_t raw = { .number = HOST_REQUEST_FIRST, .status = 429, .item = -1,
        .response = { .status = 429, .retry_after = 300 } };
    hand_over(&raw);
    assert(raw.response.retry_after == 300 && s_poll_cooldown_until_ms == 0);
    /* Reconnection can arrive before the active request notices its lost connection. */
    assert(services_poll_now("probe", 0));
    s_forward_ms += 1001;
    take_poll_deadlines();
    job = next_poll_job();
    assert(job != NULL && job->number == 0);
    s_network_generation++;
    job->status = HOST_FAILED_NO_ANSWER;
    finish_poll(job);
    free_job(job);
    job = next_poll_job();
    assert(job != NULL && job->number == 0);
    job->status = 200;
    finish_poll(job);
    free_job(job);
    assert(next_poll_job() == NULL);
    s_network_generation = 0;
    /* A broken response body cannot turn an HTTP refusal into a reconnect retry. */
    const int http_statuses[] = { 0, 200, 404, 503 };
    for (size_t index = 0; index < sizeof http_statuses / sizeof http_statuses[0]; index++) {
        s_forward_ms += 1001;
        take_poll_deadlines();
        assert(services_poll_now("probe", 0));
        job = next_poll_job();
        assert(job != NULL);
        job->status = HOST_FAILED_NO_ANSWER;
        job->response.status = http_statuses[index];
        finish_poll(job); free_job(job);
        poll_schedule_network_is_back(&s_poll_schedule);
        job = next_poll_job();
        if (http_statuses[index] == 0 || http_statuses[index] == 200) {
            assert(job != NULL);
            job->status = 200;
            finish_poll(job); free_job(job);
        } else {
            assert(job == NULL);
        }
    }
    /* An expired delay still terminates the active list round. */
    assert(services_poll_now("probe", 1));
    s_forward_ms += 1001;
    take_poll_deadlines();
    job = next_poll_job();
    assert(job != NULL && job->poll_items == 3);
    job->status = 200;
    job->response = (host_answer_info_t){ .status = 200, .retry_after = 1 };
    job->response_at_ms = monotonic_ms() - 1500;
    hand_over(job);
    finish_poll(job);
    free_job(job);
    assert(next_poll_job() == NULL);
    s_options.at = 0;
    atomic_store(&s_clock_offset_s, 0);
    s_forward_ms = 0;
    s_poll_schedule.count = 0;
    deadline_table_init(&s_deadlines);
    s_requests_waiting = 0;
    free(s_manifest_json);
    s_manifest_json = NULL;
    s_manifest = (app_manifest_t){0};
}

static int question_events;

static scene_reaction_t record_question_button(scene_t *scene, scene_button_t button, scene_press_t action)
{
    (void)scene; (void)button; (void)action;
    question_events++;
    return (scene_reaction_t){ .kind = SCENE_IGNORED };
}

static void draw_test_scene(scene_t *scene, gfx_t *surface)
{
    (void)scene; (void)surface;
}

static void draw_test_header(gfx_t *surface, const scene_t *scene, void *context)
{
    (void)surface; (void)scene; (void)context;
}

static void test_question_and_long_back(void)
{
    static const scene_ops_t ops = { .button = record_question_button, .draw = draw_test_scene };
    scene_t home = { .ops = &ops }, question = { .ops = &ops, .question = 1 };
    uint8_t frame[400 / 8 * 300];
    gfx_init(&s_screen, frame, 400, 300);
    s_question_press = (question_press_t){0};
    s_back_switch = (back_switch_state_t){0};
    wm_init(&s_wm, &home, &question, draw_test_header, NULL);
    show();
    button_event(SCENE_BUTTON_OK, SCENE_PRESS_DOWN);
    assert(question_events == 0);
    question.question++;
    wm_invalidate(&s_wm, WM_CHANGED_BY_DATA);
    show();
    button_event(SCENE_BUTTON_OK, SCENE_PRESS_UP);
    button_event(SCENE_BUTTON_OK, SCENE_PRESS_SHORT);
    assert(question_events == 0);
    button_event(SCENE_BUTTON_OK, SCENE_PRESS_DOWN);
    button_event(SCENE_BUTTON_OK, SCENE_PRESS_UP);
    button_event(SCENE_BUTTON_OK, SCENE_PRESS_SHORT);
    assert(question_events == 2);
    question_events = 0;
    press(3, true);
    assert(s_wm.current == &home && question_events == 1);
    s_wm = (wm_t){0};
    s_question_press = (question_press_t){0};
    s_back_switch = (back_switch_state_t){0};
    s_screen = (gfx_t){0};
}


static void test_forward_arguments(void)
{
    static const char *invalid[] = { "", "0", "-1", "1.5", "1garbage", "99999999999999999999999999" };
    for (size_t index = 0; index < sizeof invalid / sizeof invalid[0]; index++) {
        request_t request = { .method = "POST", .path = "/api/sim/forward" };
        snprintf(request.query, sizeof request.query, "seconds=%s", invalid[index]);
        int sockets[2];
        assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
        int64_t before = s_forward_ms;
        handle(sockets[0], &request);
        close(sockets[0]);
        char response[512] = {0};
        assert(read(sockets[1], response, sizeof response - 1) > 0);
        assert(strstr(response, "400") != NULL && s_forward_ms == before);
        close(sockets[1]);
    }
}

static void test_storage_failures(const char *state)
{
    char bytes[16], path[PATH_BYTES], folder[PATH_BYTES];
    s_manifest_json = strdup("{\"settings\":[{\"key\":\"server\",\"type\":\"url\",\"title\":\"Server\"},"
        "{\"key\":\"login\",\"type\":\"link\",\"title\":\"Login\"}]}");
    const char *users_keys[] = { "server", "login" };
    for (size_t index = 0; index < sizeof users_keys / sizeof users_keys[0]; index++) {
        set_value(users_keys[index], "http://example.test");
        assert(kv_set("probe", users_keys[index], NULL, 0) == HOST_ERR_ARGUMENT);
        assert(kv_get("probe", users_keys[index], bytes, sizeof bytes) == HOST_ERR_NO_SPACE);
        assert(kv_store("probe", users_keys[index], NULL, 0, false) == HOST_ERR_ARGUMENT);
        store_path(path, "probe", users_keys[index]);
        assert(remove(path) == 0);
    }
    free(s_manifest_json); s_manifest_json = NULL;
    assert(kv_get("probe", "absent", bytes, sizeof bytes) == HOST_ERR_NOT_FOUND);
    snprintf(folder, sizeof folder, "%s/store/probe", state);
    make_folders(folder);
    store_path(path, "probe", "broken");
    assert(symlink("/dev/null/nonexistent", path) == 0);
    assert(kv_get("probe", "broken", bytes, sizeof bytes) == HOST_ERR_FAILED);
    assert(kv_store("probe", "another", "value", 5, false) == HOST_ERR_FAILED);
    assert(unlink(path) == 0);
    assert(kv_store("probe", "another", "value", 5, false) == HOST_OK);
    store_path(path, "probe", "another"); assert(remove(path) == 0);
    snprintf(folder, sizeof folder, "%s/probe", state);
    assert(mkdir(folder, 0700) == 0);
    snprintf(path, sizeof path, "%s/probe/broken", state);
    assert(symlink("/dev/null/nonexistent", path) == 0);
    assert(services_file_size("probe", "absent") == HOST_ERR_NOT_FOUND);
    assert(services_file_read("probe", "absent", 0, bytes, sizeof bytes) == HOST_ERR_NOT_FOUND);
    assert(services_file_size("probe", "broken") == HOST_ERR_FAILED);
    assert(services_file_read("probe", "broken", 0, bytes, sizeof bytes) == HOST_ERR_FAILED);
    assert(unlink(path) == 0); assert(rmdir(folder) == 0);
    /* Consumption writes use a staged marker; failed replacement preserves the old one. */
    seen_version_t version = { .id = 42 };
    snprintf(version.path, sizeof version.path, "%s/seen", state);
    uint32_t seen = 99;
    assert(read_seen_version(version.path, &seen) && seen == 0);
    assert(mkdir(version.path, 0700) == 0);
    assert(!read_seen_version(version.path, &seen));
    assert(!note_loaded_version(&version));
    assert(rmdir(version.path) == 0);
    assert(note_loaded_version(&version));
    FILE *marker = fopen(version.path, "rb");
    seen = 0;
    assert(marker != NULL && fread(&seen, sizeof seen, 1, marker) == 1);
    assert(fclose(marker) == 0 && seen == 42);
    assert(read_seen_version(version.path, &seen) && seen == 42);
    marker = fopen(version.path, "ab"); assert(marker != NULL);
    assert(fputc(0, marker) != EOF && fclose(marker) == 0);
    assert(!read_seen_version(version.path, &seen) && seen == 42);
    marker = fopen(version.path, "wb"); assert(marker != NULL);
    assert(fputc(0, marker) != EOF && fclose(marker) == 0);
    assert(!read_seen_version(version.path, &seen) && seen == 42);
    assert(remove(version.path) == 0);
}

int main(void)
{
    s_log = calloc(LOG_KEPT_BYTES + 1, 1);
    char state[] = "/tmp/perinkpheral-jobs-XXXXXX";
    assert(mkdtemp(state) != NULL);
    s_options.state = state;
    s_options.app = "probe";
    s_options.apps = state;
    test_empty_and_long_list();
    test_time_and_recurrence();
    test_poll_scheduling();
    test_question_and_long_back();

    test_forward_arguments();
    test_storage_failures(state);
    test_network_icon();
    test_mocks(state);
    test_content_lengths();
    test_header_timestamps();
    test_preparation_memory_failure();
    s_manifest_json = strdup("{\"hosts\":[\"{{server}}\",\"{{other}}\"],\"settings\":["
                             "{\"key\":\"server\",\"type\":\"url\"},"
                             "{\"key\":\"other\",\"type\":\"url\"}]}");
    set_value("server", "http://first.example");
    set_value("other", "http://second.example");
    set_value("token", "old");
    set_value("empty", "");
    char expanded[64];
    assert(text_template_fill_url("http://first.example/{{empty}}", setting_of, NULL,
                                  expanded, sizeof expanded));
    assert(strcmp(expanded, "http://first.example/") == 0);
    assert(text_template_fill("Bearer {{empty}}", setting_of, NULL, expanded, sizeof expanded));
    assert(strcmp(expanded, "Bearer ") == 0);
    assert(text_template_fill_body("value={{empty}}", setting_of, NULL, expanded, sizeof expanded));
    assert(strcmp(expanded, "value=") == 0);
    assert(!text_template_fill("{{missing}}", setting_of, NULL, expanded, sizeof expanded));
    app_request_t request = { .tag = 7, .method = HOST_HTTP_GET, .header_count = 1 };
    snprintf(request.url, sizeof request.url, "http://first.example/data");
    snprintf(request.headers[0].name, sizeof request.headers[0].name, "Authorization");
    snprintf(request.headers[0].value, sizeof request.headers[0].value, "Bearer {{token}}");

    assert(services_request("probe", &request) == HOST_OK);
    set_value("token", "rotated");
    job_t *job = s_jobs_pending;
    s_jobs_pending = NULL;
    prepare_job(job);
    assert(strcmp(job->headers, "Authorization: Bearer rotated\n") == 0);
    assert(strcmp(job->url, "http://first.example/data") == 0);
    free_job(job);

    assert(services_request("probe", &request) == HOST_OK);
    set_value("server", "http://changed.example");
    job = s_jobs_pending;
    s_jobs_pending = NULL;
    prepare_job(job);
    assert(job->status == HOST_FAILED_NOT_ALLOWED);
    assert(job->headers == NULL);
    hand_over(job);
    assert(strstr(s_log, "a request to a host its manifest does not list") != NULL);
    assert(strstr(s_log, "redirected") == NULL);
    free_job(job);
    set_value("server", "http://first.example");

    app_poll_t poll = { .header_count = 1, .method = HOST_HTTP_POST };
    snprintf(poll.body, sizeof poll.body, "{{item}}={{token}}");
    snprintf(poll.url, sizeof poll.url, "{{server}}/api/{{item}}");
    snprintf(poll.headers[0].name, sizeof poll.headers[0].name, "Authorization");
    snprintf(poll.headers[0].value, sizeof poll.headers[0].value, "Bearer {{token}} {{item}}");
    list_item_t each = { .key = "item", .item = "a/b" };
    ask(start_poll_request(&poll, 0, &each, 2));
    set_value("server", "http://changed.example");
    set_value("token", "new-server-token");
    job = s_jobs_pending;
    s_jobs_pending = NULL;
    prepare_job(job);
    assert(job->status == 0);
    assert(strcmp(job->url, "http://changed.example/api/a%2Fb") == 0);
    assert(strcmp(job->headers, "Authorization: Bearer new-server-token a/b\n") == 0);
    s_mocks = json_parse("{\"http://changed.example/api/a%2Fb\":{\"method\":\"POST\","
                         "\"request_body\":\"a/b=new-server-token\",\"body\":\"ok\"}}");
    assert(mock_for(job) != NULL);
    job->method = HOST_HTTP_GET;
    assert(mock_for(job) == NULL);
    job->method = HOST_HTTP_POST;
    job->body[0] = 'b';
    assert(mock_for(job) == NULL);
    job->body[0] = 'a';
    json_free(s_mocks);
    s_mocks = NULL;
    assert(job->item == 2 && job->method == HOST_HTTP_POST);
    assert(job->body_length == strlen("a/b=new-server-token"));
    assert(memcmp(job->body, "a/b=new-server-token", job->body_length) == 0);
    free_job(job);

    snprintf(poll.url, sizeof poll.url, "{{missing}}");
    ask(start_poll_request(&poll, 0, NULL, -1));
    job = s_jobs_pending;
    s_jobs_pending = NULL;
    prepare_job(job);
    assert(job->status == HOST_FAILED_NOT_READY && job->headers == NULL);
    free_job(job);
    snprintf(poll.url, sizeof poll.url, "{{server}}/data");
    ask(start_poll_request(&poll, 0, NULL, -1));
    set_value("server", "");
    job = s_jobs_pending;
    s_jobs_pending = NULL;
    prepare_job(job);
    assert(job->status == HOST_FAILED_NOT_READY && job->headers == NULL);
    free_job(job);
    set_value("server", "http://first.example");
    set_value("token", "rotated");

    s_mocks = json_parse("{\"http://first.example/data\":{\"status\":302,"
                         "\"headers\":{\"Location\":\"http://second.example/final\"},"
                         "\"body\":\"redirect\"},"
                         "\"http://second.example/final\":{\"body\":\"followed\"}}");
    assert(services_request("probe", &request) == HOST_OK);
    job = s_jobs_pending;
    s_jobs_pending = NULL;
    prepare_job(job);
    fetch(job);
    assert(job->status == 302);
    assert(strcmp(job->url, "http://first.example/data") == 0);
    assert(job->answer_length == strlen("redirect"));
    assert(memcmp(job->answer, "redirect", job->answer_length) == 0);
    char location[64] = { 0 };
    assert(answer_headers_get(&job->response_headers, "Location", strlen("Location"), 0,
                              location, sizeof location) == strlen("http://second.example/final"));
    assert(strcmp(location, "http://second.example/final") == 0);
    free_job(job);
    json_free(s_mocks);
    s_mocks = NULL;

    /* Pipes hold a transmission open while the main loop reloads and replaces its manifest. */
    int requests[2], answers[2];
    assert(pipe(requests) == 0 && pipe(answers) == 0 && pipe(s_jobs_done) == 0);
    s_fetcher_in = fdopen(requests[1], "w");
    s_fetcher_out = fdopen(answers[0], "r");
    FILE *asked = fdopen(requests[0], "r");
    FILE *answered = fdopen(answers[1], "w");
    pthread_t worker;
    assert(pthread_create(&worker, NULL, network_thread, NULL) == 0);
    snprintf(poll.url, sizeof poll.url, "{{server}}/post");
    snprintf(poll.body, sizeof poll.body, "token={{token}}");
    snprintf(poll.headers[0].value, sizeof poll.headers[0].value, "Bearer {{token}}");
    ask(start_poll_request(&poll, 0, NULL, -1));
    set_value("token", "send-time");
    dispatch_job();
    json_t *post = read_request(asked);
    assert(strcmp(json_get(post, "method")->text, "POST") == 0);
    assert(strcmp(json_get(json_get(post, "headers"), "Authorization")->text, "Bearer send-time") == 0);
    size_t body_length;
    uint8_t *sent_body = base64_decode(json_get(post, "body")->text, &body_length);
    assert(body_length == strlen("token=send-time") && memcmp(sent_body, "token=send-time", body_length) == 0);
    free(sent_body);
    json_free(post);
    reply(answered, "{\"status\":307,\"next\":\"http://second.example/replay\",\"body\":\"\"}");
    assert(read(s_jobs_done[0], &job, sizeof job) == sizeof job);
    assert(job->status == 307);
    free_job(job);
    s_job_active = false;
    set_value("token", "rotated");
    assert(services_request("probe", &request) == HOST_OK);
    dispatch_job();
    json_t *sent = read_request(asked);
    assert(strcmp(json_get(json_get(sent, "headers"), "Authorization")->text, "Bearer rotated") == 0);
    json_free(sent);
    assert(services_request("probe", &request) == HOST_OK);
    reset_jobs();
    assert(s_jobs_pending == NULL && s_requests_waiting == 0);
    free(s_manifest_json);
    s_manifest_json = strdup("{\"hosts\":[]}");
    set_value("other", "http://changed.example");
    reply(answered, "{\"status\":302,\"next\":\"http://second.example/redirect\",\"body\":\"\"}");
    sent = read_request(asked);
    assert(strcmp(json_get(sent, "url")->text, "http://second.example/redirect") == 0);
    assert(json_count(json_get(sent, "headers")) == 0);
    json_free(sent);
    reply(answered, "{\"status\":200,\"body\":\"\"}");
    assert(read(s_jobs_done[0], &job, sizeof job) == sizeof job);
    assert(job->status == 200 && !accepts_job(job));
    free_job(job);
    s_job_active = false;
    job = calloc(1, sizeof *job);
    ask(job);
    assert(accepts_job(job));
    reset_jobs();
    free(s_manifest_json);
    char path[PATH_BYTES];
    const char *keys[] = { "server", "other", "token", "empty" };
    for (size_t index = 0; index < sizeof keys / sizeof keys[0]; index++) {
        store_path(path, "probe", keys[index]);
        remove(path);
    }
    snprintf(path, sizeof path, "%s/store/probe", state);
    rmdir(path);
    snprintf(path, sizeof path, "%s/store", state);
    rmdir(path);
    rmdir(state);
    free(s_log);
    puts("Simulator jobs: poll destinations, current credentials, refusal logs, reload isolation and redirect snapshot passed");
    return 0;
}
