/* SPDX-License-Identifier: MIT */
#define main simulator_main
#include "sim.c"
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
    const char *invalid[] = { "", "{", "[]", "null", "{} trailing", "{\"http://example.test\":null}" };
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

int main(void)
{
    s_log = calloc(LOG_KEPT_BYTES + 1, 1);
    char state[] = "/tmp/perinkpheral-jobs-XXXXXX";
    assert(mkdtemp(state) != NULL);
    s_options.state = state;
    s_options.app = "probe";
    s_options.apps = state;
    test_poll_scheduling();
    test_network_icon();
    test_mocks(state);
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
