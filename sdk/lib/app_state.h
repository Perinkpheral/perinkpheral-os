/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * State and settings in the application's store, by null-terminated key.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "host_api.h"

/* Loads a versioned struct stored under `key`. The first field is an int32_t
 * version. HOST_OK means exactly `size` bytes with this version were restored.
 * HOST_ERR_NOT_FOUND means absent or incompatible (size or version); other
 * negative results preserve kv_get() failures, including HOST_ERR_FAILED.
 * On failure `state` may hold any part of the value: initialise it whole.
 * After a storage failure, do not save or forget this key until a subsequent
 * restore succeeds or establishes absence/incompatibility. Defaults used for
 * display must not replace unreadable durable state.
 * state must point to an aligned structure of at least sizeof(int32_t) bytes;
 * size must include its version field. */
int32_t app_state_load(const char *key, void *state, int32_t size, int32_t version);

/* Stores `size` bytes of `state` under `key` with one kv_set() call.
 * Save when the state changes, in app_on_terminate() or at once for a value
 * that must survive a power loss. Returns true on HOST_OK and false
 * whenever kv_set() fails, including invalid arguments, insufficient
 * storage and storage errors. A failure may have changed storage; the
 * caller must handle the failed save. */
bool app_state_save(const char *key, const void *state, int32_t size);

/* Removes the value stored under `key`; false when the storage failed. */
bool app_state_forget(const char *key);

/* Where the user is in the application - a page, a selection, a value half
 * set - kept while it is in the stack, in memory (session_set, host/app.h).
 * Save it in app_on_blur(), load it in app_on_focus(). The struct is at most
 * HOST_SESSION_MAX bytes: APP_SESSION_FITS(type) says so when it builds.
 * app_session_load() is true when what is kept is exactly `size` bytes. On
 * false `state` may hold a shorter session of another shape: initialise it. */
bool app_session_save(const void *state, int32_t size);
bool app_session_load(void *state, int32_t size);
#define APP_SESSION_FITS(type) \
    _Static_assert(sizeof(type) <= HOST_SESSION_MAX, #type " is larger than HOST_SESSION_MAX")

/* Copies a setting to `text` as a null-terminated string. Returns its
 * length; 0 if the setting is not set or does not fit, `text` then being
 * empty. A `list` setting comes as its items, each ended by '\n' but the
 * last: see app_setting_item(). */
int32_t app_setting(const char *key, char *text, int32_t text_size);

/* True if the setting equals `wanted`. */
bool app_setting_is(const char *key, const char *wanted);

/* A `toggle` setting: true unless it is set to "0". */
bool app_setting_is_on(const char *key);

/* A `number` setting scaled by 10^decimals, as app_parse_number() reads it.
 * Returns false if the setting is not set or is not a number. */
bool app_setting_number(const char *key, int32_t decimals, int32_t *number);

/* A `list` setting: the number of its items, and item number `index` as a
 * null-terminated string. app_setting_item() returns its length; 0 if there
 * is no such item or it does not fit, `text` then being empty. */
int32_t app_setting_count(const char *key);
int32_t app_setting_item(const char *key, int32_t index, char *text, int32_t text_size);

/* A pace or a delay as a person writes it - "30 s", "15 min", "1 h", "1 d" -
 * in seconds, parsed by the same rules as a poll's "every" value; `otherwise` when the
 * text says no such thing ("Off", "At start"). */
int32_t app_seconds_of(const char *text, int32_t otherwise);

/* The same of a setting; `otherwise` too when it is not set. */
int32_t app_setting_seconds(const char *key, int32_t otherwise);

/* A `place` setting's name: what stands before the first comma of "Saint
 * Petersburg, Saint Petersburg, Russia", cut between characters to fit.
 * Returns its length; 0 when the setting is not set.
 * name must point to at least size writable bytes; size must be positive. */
int32_t app_setting_place(const char *key, char *name, int32_t size);
