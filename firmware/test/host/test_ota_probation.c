// SPDX-License-Identifier: MIT
#include <setjmp.h>
#include <string.h>
#include "tinytest.h"
#include "ota.h"
#include "ota_health.h"
#include "esp_app_desc.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "freertos/task.h"
#include "wifi.h"

enum { TASK_RESTARTED = 1, TASK_CONFIRMED, TASK_STALLED, ATTEMPTS_MAX = 3 };
static const esp_partition_t factory = { .subtype = ESP_PARTITION_SUBTYPE_APP_FACTORY };
static const esp_partition_t main_slot = { .subtype = ESP_PARTITION_SUBTYPE_APP_OTA_0 };
static const esp_app_desc_t description = { .version = "test" };
static esp_ota_img_states_t image_state;
static bool active_record, missing_factory, refuse_factory, web_up, missing_running;
static unsigned state_query_failures;
static unsigned failed_writes, confirmations, rejections, factory_selections, delays;
static unsigned missing_record_attempts;
static int64_t now_us;
static void (*probation_task)(void *);
static jmp_buf task_exit;

const esp_app_desc_t *esp_app_get_description(void) { return &description; }
const esp_partition_t *esp_ota_get_running_partition(void)
{
    return missing_running ? NULL : &main_slot;
}

esp_err_t esp_ota_get_state_partition(const esp_partition_t *partition, esp_ota_img_states_t *state)
{
    CHECK(partition == &main_slot);
    if (state_query_failures > 0) {
        state_query_failures--;
        return ESP_FAIL;
    }
    if (!active_record) { return ESP_FAIL; }
    *state = image_state;
    return ESP_OK;
}

const esp_partition_t *esp_partition_find_first(int type, int subtype, const char *label)
{
    CHECK(type == ESP_PARTITION_TYPE_APP && subtype == ESP_PARTITION_SUBTYPE_APP_FACTORY);
    CHECK(label == NULL);
    return missing_factory ? NULL : &factory;
}

esp_err_t esp_ota_set_boot_partition(const esp_partition_t *partition)
{
    CHECK(partition == &factory && ota_is_on_probation());
    factory_selections++;
    if (refuse_factory) { return ESP_FAIL; }
    active_record = false;
    return ESP_OK;
}

esp_err_t esp_partition_write(const esp_partition_t *partition, size_t offset,
                              const void *data, size_t length)
{
    (void)partition; (void)offset; (void)data; (void)length;
    CHECK(false);
    return ESP_FAIL;
}

static esp_err_t write_verdict(void)
{
    CHECK(ota_is_on_probation());
    CHECK(ota_enter_recovery(false) != NULL);
    if (!active_record || image_state == ESP_OTA_IMG_INVALID || image_state == ESP_OTA_IMG_ABORTED) {
        missing_record_attempts++;
        return ESP_FAIL;
    }
    if (failed_writes > 0) {
        failed_writes--;
        /* IDF erases the active record before attempting the replacement write. */
        active_record = false;
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t esp_ota_mark_app_valid_cancel_rollback(void)
{
    confirmations++;
    return write_verdict();
}

esp_err_t esp_ota_mark_app_invalid_rollback_and_reboot(void)
{
    rejections++;
    esp_err_t result = write_verdict();
    if (result == ESP_OK) { esp_restart(); }
    return result;
}

void esp_restart(void) { longjmp(task_exit, TASK_RESTARTED); }
void vTaskDelete(void *handle)
{
    CHECK(handle == NULL && !ota_is_on_probation());
    longjmp(task_exit, TASK_CONFIRMED);
}

BaseType_t xTaskCreate(void (*task)(void *), const char *name, unsigned stack,
                       void *argument, unsigned priority, void *handle)
{
    (void)name; (void)stack; (void)priority;
    CHECK(argument == NULL && handle == NULL && probation_task == NULL);
    probation_task = task;
    return pdPASS;
}

void vTaskDelay(unsigned ticks)
{
    CHECK(ticks == 1000);
    if (++delays > ATTEMPTS_MAX + 1) { longjmp(task_exit, TASK_STALLED); }
    now_us += (int64_t)ticks * 1000;
}

int64_t esp_timer_get_time(void) { return now_us; }
bool web_server_is_up(void) { return web_up; }
void wifi_get_status(wifi_status_t *status)
{
    memset(status, 0, sizeof *status);
    status->phase = WIFI_PHASE_ONLINE;
}

static void reset(void)
{
    image_state = ESP_OTA_IMG_PENDING_VERIFY;
    active_record = true;
    missing_factory = refuse_factory = web_up = missing_running = false;
    state_query_failures = 0;
    failed_writes = confirmations = rejections = factory_selections = delays = 0;
    missing_record_attempts = 0;
    now_us = (int64_t)(OTA_HEALTH_DEADLINE_S - 1) * 1000000;
    probation_task = NULL;
}

static int run_probation(void)
{
    ota_start(true);
    CHECK(ota_is_on_probation() && probation_task != NULL);
    if (probation_task == NULL) { return TASK_STALLED; }
    int result = setjmp(task_exit);
    if (result == 0) {
        probation_task(NULL);
        CHECK(false);
    }
    return result;
}

int main(void)
{
    reset();
    CHECK(run_probation() == TASK_RESTARTED);
    CHECK(rejections == 1 && confirmations == 0 && factory_selections == 0);

    /* A lost record must not trap probation, even if selecting factory also fails. */
    for (unsigned fallback = 0; fallback < 3; fallback++) {
        reset();
        failed_writes = 1;
        refuse_factory = fallback == 1;
        missing_factory = fallback == 2;
        CHECK(run_probation() == TASK_RESTARTED);
        CHECK(rejections == ATTEMPTS_MAX && confirmations == 0);
        CHECK(missing_record_attempts == ATTEMPTS_MAX - 1);
        CHECK(factory_selections == (missing_factory ? 0 : 1));
        CHECK(delays == ATTEMPTS_MAX && ota_is_on_probation());
    }

    /* Confirmation uses the same escape when its metadata write loses the record. */
    reset();
    web_up = true;
    failed_writes = 1;
    CHECK(run_probation() == TASK_RESTARTED);
    CHECK(confirmations == ATTEMPTS_MAX && rejections == 0 && factory_selections == 1);
    CHECK(missing_record_attempts == ATTEMPTS_MAX - 1 && ota_is_on_probation());

    reset();
    web_up = true;
    CHECK(run_probation() == TASK_CONFIRMED);
    CHECK(confirmations == 1 && rejections == 0 && factory_selections == 0);
    CHECK(!ota_is_on_probation());

    /* A failed bootloader journal update may leave NEW or another unverified state. */
    const esp_ota_img_states_t unverified_states[] = {
        ESP_OTA_IMG_NEW, ESP_OTA_IMG_PENDING_VERIFY, ESP_OTA_IMG_UNDEFINED,
    };
    for (unsigned index = 0; index < sizeof unverified_states / sizeof unverified_states[0]; index++) {
        reset();
        image_state = unverified_states[index];
        web_up = true;
        CHECK(run_probation() == TASK_CONFIRMED);
        CHECK(confirmations == 1 && rejections == 0 && factory_selections == 0);
        CHECK(!ota_is_on_probation());
    }

    const esp_ota_img_states_t rejected_states[] = { ESP_OTA_IMG_INVALID, ESP_OTA_IMG_ABORTED };
    for (unsigned index = 0; index < sizeof rejected_states / sizeof rejected_states[0]; index++) {
        reset();
        image_state = rejected_states[index];
        web_up = true;
        CHECK(run_probation() == TASK_RESTARTED);
        CHECK(confirmations == ATTEMPTS_MAX && factory_selections == 1);
        CHECK(ota_is_on_probation());
    }

    /* A transient state-query failure still requires the health check and confirmation. */
    reset();
    image_state = ESP_OTA_IMG_VALID;
    state_query_failures = 1;
    web_up = true;
    CHECK(run_probation() == TASK_CONFIRMED);
    CHECK(confirmations == 1 && !ota_is_on_probation());

    /* Erasing NEW then losing PENDING_VERIFY must reach the bounded recovery fallback. */
    for (unsigned reachable = 0; reachable < 2; reachable++) {
        reset();
        active_record = false;
        web_up = reachable != 0;
        CHECK(run_probation() == TASK_RESTARTED);
        CHECK(confirmations == (web_up ? ATTEMPTS_MAX : 0));
        CHECK(rejections == (web_up ? 0 : ATTEMPTS_MAX));
        CHECK(factory_selections == 1 && missing_record_attempts == ATTEMPTS_MAX);
        CHECK(delays == ATTEMPTS_MAX && ota_is_on_probation());
    }

    reset();
    missing_running = true;
    active_record = false;
    web_up = true;
    CHECK(run_probation() == TASK_RESTARTED);
    CHECK(confirmations == ATTEMPTS_MAX && factory_selections == 1);
    CHECK(ota_is_on_probation());

    reset();
    image_state = ESP_OTA_IMG_VALID;
    ota_start(true);
    CHECK(!ota_is_on_probation() && probation_task == NULL);
    REPORT();
}
