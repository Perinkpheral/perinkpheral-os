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

int main(void)
{
    s_log = calloc(LOG_KEPT_BYTES + 1, 1);
    char state[] = "/tmp/perinkpheral-jobs-XXXXXX";
    assert(mkdtemp(state) != NULL);
    s_options.state = state;
    s_options.app = "probe";
    s_options.apps = state;
    s_manifest_json = strdup("{\"hosts\":[\"{{server}}\",\"{{other}}\"],\"settings\":["
                             "{\"key\":\"server\",\"type\":\"url\"},"
                             "{\"key\":\"other\",\"type\":\"url\"}]}");
    set_value("server", "http://first.example");
    set_value("other", "http://second.example");
    set_value("token", "old");
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
    start_poll_request(&poll, 0, &each, 2);
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
    start_poll_request(&poll, 0, NULL, -1);
    job = s_jobs_pending;
    s_jobs_pending = NULL;
    prepare_job(job);
    assert(job->status == HOST_FAILED_NOT_READY && job->headers == NULL);
    free_job(job);
    snprintf(poll.url, sizeof poll.url, "{{server}}/data");
    start_poll_request(&poll, 0, NULL, -1);
    set_value("server", "");
    job = s_jobs_pending;
    s_jobs_pending = NULL;
    prepare_job(job);
    assert(job->status == HOST_FAILED_NOT_READY && job->headers == NULL);
    free_job(job);
    set_value("server", "http://first.example");
    set_value("token", "rotated");

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
    start_poll_request(&poll, 0, NULL, -1);
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
    const char *keys[] = { "server", "other", "token" };
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
