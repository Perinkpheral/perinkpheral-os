/* SPDX-License-Identifier: MIT */
#include <stdlib.h>
#include "tinytest.h"
#include "esp_log.h"
#ifndef ESP_LOGD
#define ESP_LOGD ESP_LOGW
#endif
#include "../../main/app_host.c"

static long storage_result;
static bool embedded;
const uint8_t *launcher_system_file(const char *app_name, const char *file, size_t *size)
{
    (void)app_name; (void)file;
    static const uint8_t content[] = { 1, 2, 3 };
    *size = sizeof content;
    return embedded ? content : NULL;
}
long app_store_file_size(const char *root, const char *app_name, const char *file)
{
    (void)root; (void)app_name; (void)file;
    return storage_result;
}
long app_store_read_file(const char *root, const char *app_name, const char *file, long offset,
                         void *bytes, size_t capacity)
{
    (void)root; (void)app_name; (void)file; (void)offset;
    if (capacity > 0) ((uint8_t *)bytes)[0] = 42; /* a failed read may copy a prefix */
    return storage_result;
}
int main(void)
{
    char bytes[4] = {0};
    const long results[] = { APP_FILE_NOT_FOUND, APP_FILE_FAILED, 0, 3 };
    const int32_t expected[] = { HOST_ERR_NOT_FOUND, HOST_ERR_FAILED, 0, 3 };
    for (size_t index = 0; index < sizeof results / sizeof results[0]; index++) {
        storage_result = results[index];
        CHECK(file_size("app", "data") == expected[index]);
        CHECK(file_read("app", "data", 0, bytes, sizeof bytes) == expected[index]);
    }
    storage_result = APP_FILE_FAILED;
    CHECK(file_size("sys.app", "absent") == HOST_ERR_NOT_FOUND);
    CHECK(file_read("sys.app", "absent", 0, bytes, sizeof bytes) == HOST_ERR_NOT_FOUND);
    embedded = true;
    CHECK(file_size("sys.app", "data") == 3);
    CHECK(file_read("sys.app", "data", 1, bytes, sizeof bytes) == 2);
    CHECK(bytes[0] == 2 && bytes[1] == 3);
    CHECK(file_read("sys.app", "data", 99, bytes, sizeof bytes) == 0);
    REPORT();
}
