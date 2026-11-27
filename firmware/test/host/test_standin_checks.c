/* SPDX-License-Identifier: MIT */
#define main capability_checks_main
#include "../../../sdk/check/checks.c"
#undef main
#include "tinytest.h"

static const standin_poll_t polls[] = {
    { .url = "https://example.test/{{items}}", .each = "items", .manual = true },
};
static const standin_setting_t settings[] = {
    { .key = "items", .kind = STANDIN_SETTING_LIST, .otherwise = "first\nsecond" },
};
static const standin_answer_t answers[] = {
    { .address = "https://example.test/first", .status = HOST_FAILED_NO_ANSWER, .body = "" },
    { .address = "https://example.test/second", .status = 200, .body = "ok" },
};
const standin_subject_t standin_subject = {
    .polls = polls, .poll_count = 1, .settings = settings, .setting_count = 1,
    .answers = answers, .answer_count = 2,
};

void standin_subject_handlers(void) { }

static int delivered;

static void receive(int32_t request, const uint8_t *data, int32_t length, int32_t flags, int32_t status)
{
    (void)request; (void)data; (void)length; (void)flags; (void)status;
    delivered++;
}

static void delete_after_failed_restore(void)
{
    int32_t saved;
    if (kv_get("saved", 5, &saved, sizeof saved) == HOST_ERR_FAILED) kv_set("saved", 5, NULL, 0);
}

static void seed_on_button(int32_t event)
{
    if (event == BTN_OK_PRESSED) {
        int32_t saved = 42;
        kv_set("saved", 5, &saved, sizeof saved);
    }
}

int main(void)
{
    standin_reset();
    standin_handlers = (standin_handlers_t){ .on_data = receive };
    standin_load(); standin_focus();
    CHECK(poll_run(0) == HOST_OK);
    answer_manifest_polls(&(round_t){ .kind = ROUND_WHOLE }, HOST_OK);
    CHECK(delivered == 1);
    CHECK(standin()->poll_network_failed[0]);
    standin_intent(HOST_INTENT_NETWORK_UP);
    CHECK(standin_poll_take(0));
    CHECK(!standin_poll_take(0));
    checking = STORE;
    standin_handlers = (standin_handlers_t){ .on_load = delete_after_failed_restore, .on_button = seed_on_button };
    check_failed_restore();
    CHECK(results[STORE].problem_count == 1);
    CHECK(strstr(results[STORE].problems[0], "restore-read:") != NULL);
    checking = NETWORK;
    standin_handlers = (standin_handlers_t){0};
    check_network();
    bool missing_coverage = false;
    for (int index = 0; index < results[NETWORK].problem_count; index++) {
        missing_coverage = missing_coverage || strstr(results[NETWORK].problems[index], "manual-poll coverage:") != NULL;
    }
    CHECK(missing_coverage);
    REPORT();
}
