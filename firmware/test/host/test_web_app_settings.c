/* SPDX-License-Identifier: MIT */
#include <string.h>
#include "tinytest.h"
#include "app_kv.h"
#include "host_api.h"
#include "web_app_settings.h"

static const char MANIFEST[] =
    "{\"settings\":[{\"key\":\"server\",\"type\":\"url\"},"
    "{\"key\":\"feed\",\"type\":\"link\"},{\"key\":\"token\",\"type\":\"secret\"},"
    "{\"key\":\"label\",\"type\":\"text\"}]}";
static char server[256], token[256], feed[256], label[256];
static const char *failed_key;
static int32_t read_error, write_error;
static bool fail_empty, token_attempted;
static int writes, deletions;

int32_t app_kv_get(const char *app_name, const char *key, void *value, int32_t capacity)
{
    (void)app_name;
    if (read_error != 0) return read_error;
    const char *stored = strcmp(key, "server") == 0 ? server : strcmp(key, "feed") == 0 ? feed : strcmp(key, "token") == 0 ? token : label;
    size_t length = strlen(stored);
    if (length > (size_t)capacity) return HOST_ERR_NO_SPACE;
    memcpy(value, stored, length);
    return (int32_t)length;
}

int32_t app_kv_get_or_default(const char *app_name, const char *key, void *value, int32_t capacity)
{
    return app_kv_get(app_name, key, value, capacity);
}

int32_t app_kv_set_setting(const char *app_name, const char *key, const char *value)
{
    (void)app_name;
    if (strcmp(key, "token") == 0 && value[0] != '\0') token_attempted = true;
    if (failed_key != NULL && strcmp(key, failed_key) == 0 && (fail_empty || value[0] != '\0')) {
        return write_error;
    }
    /* No old credential may survive even the first changed destination. */
    if (strcmp(key, "server") == 0 && server[0] != '\0' && strcmp(server, value) != 0) CHECK_STR(token, "");
    char *stored = strcmp(key, "server") == 0 ? server : strcmp(key, "feed") == 0 ? feed : strcmp(key, "token") == 0 ? token : label;
    strcpy(stored, value);
    writes++;
    return HOST_OK;
}

int32_t app_kv_set(const char *app_name, const char *key, const void *value, int32_t length)
{
    CHECK(length == 0);
    int32_t result = app_kv_set_setting(app_name, key, value);
    if (result == HOST_OK) deletions++;
    return result;
}

static void reset(void)
{
    strcpy(server, "https://a.example");
    strcpy(feed, "https://a.example/feed");
    strcpy(token, "token-a");
    label[0] = '\0';
    failed_key = NULL;
    read_error = 0;
    write_error = HOST_ERR_FAILED;
    fail_empty = token_attempted = false;
    writes = deletions = 0;
}

static int32_t save(const char *text, bool *changed)
{
    json_t *wanted = json_parse(text);
    CHECK(wanted != NULL);
    int32_t result = web_app_settings_save(MANIFEST, "test", wanted, changed);
    json_free(wanted);
    return result;
}

static void test_save(void)
{
    bool changed;
    const char *replacement = "{\"token\":\"longer-token-b\",\"server\":\"https://b.example\"}";
    reset();
    failed_key = "token";
    write_error = HOST_ERR_NO_SPACE;
    CHECK(save(replacement, &changed) == HOST_ERR_NO_SPACE);
    CHECK(changed && token_attempted);
    CHECK_STR(server, "https://b.example");
    CHECK_STR(token, "");

    reset();
    failed_key = "token";
    fail_empty = true;
    CHECK(save(replacement, &changed) == HOST_ERR_FAILED);
    CHECK(!changed && writes == 0 && !token_attempted);
    CHECK_STR(server, "https://a.example");
    CHECK_STR(token, "token-a");

    reset();
    failed_key = "server";
    CHECK(save(replacement, &changed) == HOST_ERR_FAILED);
    CHECK(changed && !token_attempted);
    CHECK_STR(server, "https://a.example");
    CHECK_STR(token, "");

    reset();
    failed_key = "feed";
    CHECK(save("{\"token\":\"token-b\",\"server\":\"https://b.example\",\"feed\":\"https://b.example/feed\"}",
               &changed) == HOST_ERR_FAILED);
    CHECK(changed && !token_attempted);
    CHECK_STR(token, "");

    reset();
    CHECK(save(replacement, &changed) == HOST_OK);
    CHECK(changed);
    CHECK_STR(server, "https://b.example");
    CHECK_STR(token, "longer-token-b");

    reset();
    CHECK(save("{\"server\":\"https://b.example\"}", &changed) == HOST_OK);
    CHECK_STR(token, "");
    CHECK(deletions == 1);

    reset();
    CHECK(save("{\"server\":\"https://a.example\"}", &changed) == HOST_OK);
    CHECK_STR(token, "token-a");

    reset();
    server[0] = '\0';
    CHECK(save("{\"server\":\"https://b.example\"}", &changed) == HOST_OK);
    CHECK_STR(token, "token-a");
    CHECK(deletions == 0);

    reset();
    CHECK(save("{\"token\":\"\"}", &changed) == HOST_OK);
    CHECK_STR(token, "");
    CHECK(deletions == 1);

    reset();
    failed_key = "token";
    CHECK(save("{\"token\":\"replacement\",\"feed\":\"https://a.example/feed\",\"label\":\"kept\"}", &changed) == HOST_ERR_FAILED);
    CHECK(changed && writes == 2);
    CHECK_STR(label, "kept");
    CHECK_STR(token, "token-a");

    reset();
    read_error = HOST_ERR_FAILED;
    CHECK(save(replacement, &changed) == HOST_ERR_FAILED);
    CHECK(!changed && writes == 0);
    read_error = HOST_ERR_NO_SPACE;
    CHECK(save(replacement, &changed) == HOST_ERR_NO_SPACE);
    CHECK(!changed && writes == 0);
}

static void test_read(void)
{
    json_t *values = json_new_object();
    json_t *setting = json_parse("{\"default\":\"https://public.example/feed\"}");
    const char *types[] = { "text", "secret", "link" };
    reset();
    for (size_t index = 0; index < sizeof types / sizeof types[0]; index++) {
        read_error = HOST_ERR_FAILED;
        CHECK(web_app_settings_value(values, setting, "test", "key", types[index]));
        CHECK(json_is_true(json_get(setting, "unavailable")));
        json_remove(setting, "unavailable");
        read_error = HOST_ERR_NO_SPACE;
        CHECK(web_app_settings_value(values, setting, "test", "key", types[index]));
        if (strcmp(types[index], "text") == 0) {
            CHECK(json_is_true(json_get(setting, "unavailable")));
            CHECK(json_get(values, "key") == NULL);
            json_remove(setting, "unavailable");
        } else {
            CHECK(json_is_true(json_get(values, "key")));
            json_remove(values, "key");
        }
        CHECK(json_count(values) == 0);
    }
    read_error = HOST_ERR_NOT_FOUND;
    CHECK(web_app_settings_value(values, setting, "test", "missing", "text"));
    CHECK(json_get(values, "missing") == NULL);
    CHECK(web_app_settings_value(values, setting, "test", "link", "link"));
    CHECK_STR(json_get(values, "link")->text, "https://public.example/feed");
    read_error = 0;
    token[0] = '\0';
    CHECK(web_app_settings_value(values, setting, "test", "empty", "text"));
    CHECK_STR(json_get(values, "empty")->text, "");
    json_free(values);
    json_free(setting);
}

int main(void)
{
    test_save();
    test_read();
    REPORT();
}
