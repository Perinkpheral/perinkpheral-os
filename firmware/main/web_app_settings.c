/* SPDX-License-Identifier: MIT */
#include "web_app_settings.h"

#include <string.h>
#include "app_kv.h"
#include "host_api.h"

bool web_app_settings_value(json_t *values, json_t *setting, const char *app_name,
                              const char *key, const char *type)
{
    char value[APP_SETTING_VALUE_MAX + 1];
    int32_t length = app_kv_get(app_name, key, value, sizeof value - 1);
    bool is_link = strcmp(type, "link") == 0;
    bool is_secret = strcmp(type, "secret") == 0;
    if (length == HOST_ERR_NO_SPACE && (is_link || is_secret)) {
        return json_add_bool(values, key, true) != NULL;
    }
    if (length < 0 && length != HOST_ERR_NOT_FOUND) {
        return json_add_bool(setting, "unavailable", true) != NULL;
    }
    value[length > 0 ? length : 0] = '\0';
    if (!is_link && !is_secret) {
        /* Nothing stored is left out, for the default to show; an empty value is the user's. */
        return length == HOST_ERR_NOT_FOUND || json_add_string(values, key, value) != NULL;
    }

    /* Only a manifest default is public. Custom links, like secrets, are
     * represented by whether they have been set. */
    const json_t *preset = json_get(setting, "default");
    if (is_link && length == HOST_ERR_NOT_FOUND && json_is_string(preset)) {
        return json_add_string(values, key, preset->text) != NULL;
    }
    return json_add_bool(values, key, value[0] != '\0') != NULL;
}

static const json_t *setting_for_key(const json_t *settings, const char *key)
{
    const json_t *setting;
    json_for_each(setting, settings) {
        const json_t *name = json_get(setting, "key");
        if (json_is_string(name) && strcmp(name->text, key) == 0) {
            return setting;
        }
    }
    return NULL;
}

static bool is_destination(const json_t *settings, const char *key)
{
    const json_t *type = json_get(setting_for_key(settings, key), "type");
    return json_is_string(type) && (strcmp(type->text, "url") == 0 || strcmp(type->text, "link") == 0);
}

static int32_t changes_server(const json_t *settings, const char *app_name, const json_t *wanted,
                               bool *changes)
{
    *changes = false;
    const json_t *value;
    json_for_each(value, wanted) {
        if (!is_destination(settings, value->name)) {
            continue;
        }
        char stored[APP_SETTING_VALUE_MAX + 1];
        int32_t length = app_kv_get(app_name, value->name, stored, sizeof stored - 1);
        if (length == HOST_ERR_NOT_FOUND) {
            const json_t *preset = json_get(setting_for_key(settings, value->name), "default");
            if (json_is_string(preset)) {
                size_t preset_length = strlen(preset->text);
                if (preset_length >= sizeof stored) {
                    return HOST_ERR_NO_SPACE;
                }
                memcpy(stored, preset->text, preset_length);
                length = (int32_t)preset_length;
            }
        }
        if (length < 0 && length != HOST_ERR_NOT_FOUND) {
            return length;
        }
        stored[length > 0 ? length : 0] = '\0';
        *changes = *changes || (stored[0] != '\0' && strcmp(stored, value->text) != 0);
    }
    return HOST_OK;
}

static int32_t save_values(const json_t *settings, const char *app_name, const json_t *wanted,
                              bool *changed)
{
    *changed = false;
    bool changes;
    int32_t result = changes_server(settings, app_name, wanted, &changes);
    if (result != HOST_OK) {
        return result;
    }
    int32_t failure = HOST_OK;
    if (changes) {
        /* Clear even supplied secrets first: a failed replacement must not leave the old token. */
        const json_t *setting;
        json_for_each(setting, settings) {
            const json_t *type = json_get(setting, "type"), *key = json_get(setting, "key");
            if (!json_is_string(type) || strcmp(type->text, "secret") != 0 || !json_is_string(key)) {
                continue;
            }
            result = app_kv_set(app_name, key->text, "", 0);
            if (result == HOST_OK) {
                *changed = true;
            } else if (failure == HOST_OK) {
                failure = result;
            }
        }
    }
    if (failure != HOST_OK) {
        return failure;
    }
    /* Destinations must all be stored before replacement credentials can be published. */
    for (int pass = 0; pass < 2; pass++) {
        const json_t *value;
        json_for_each(value, wanted) {
            if (is_destination(settings, value->name) != (pass == 0)) {
                continue;
            }
            const json_t *type = json_get(setting_for_key(settings, value->name), "type");
            bool is_secret = json_is_string(type) && strcmp(type->text, "secret") == 0;
            result = is_secret && value->text[0] == '\0'
                         ? app_kv_set(app_name, value->name, "", 0)
                         : app_kv_set_setting(app_name, value->name, value->text);
            if (result == HOST_OK) {
                *changed = true;
            } else if (failure == HOST_OK) {
                failure = result;
            }
        }
        /* Try every write in a phase, but never publish credentials after a failed destination. */
        if (failure != HOST_OK) {
            return failure;
        }
    }
    return HOST_OK;
}

int32_t web_app_settings_save(const char *manifest, const char *app_name, const json_t *wanted,
                              bool *changed)
{
    *changed = false;
    json_t *root = json_parse(manifest);
    if (root == NULL) {
        return HOST_ERR_FAILED;
    }
    int32_t result = save_values(json_get(root, "settings"), app_name, wanted, changed);
    json_free(root);
    return result;
}
