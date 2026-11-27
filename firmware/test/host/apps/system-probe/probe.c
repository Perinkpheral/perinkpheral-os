/* SPDX-License-Identifier: MIT */
#include <stdbool.h>
#include "host_api.h"

void app_on_load(void)
{
    const char key[] = { 'f', 'o', 'n', 't', '\0', 'x' };
    const char value[] = { '1', '\0', 'x' };
    char output[16];
    sys_number_t number;
    bool valid = sys_setting_get(key, sizeof key, output, sizeof output) == HOST_ERR_ARGUMENT &&
                 sys_setting_set(key, sizeof key, "1", 1) == HOST_ERR_ARGUMENT &&
                 sys_setting_range(key, sizeof key, &number) == HOST_ERR_ARGUMENT &&
                 sys_setting_set("font", 4, value, sizeof value) == HOST_ERR_ARGUMENT &&
                 sys_setting_get(key, 5, output, sizeof output) == HOST_ERR_ARGUMENT &&
                 sys_setting_set("font", 4, value, 2) == HOST_ERR_ARGUMENT &&
                 sys_setting_get("font", 4, output, sizeof output) == HOST_OK &&
                 sys_setting_set("font", 4, "1", 1) == HOST_OK &&
                 sys_setting_range("font", 4, &number) == HOST_OK;
    if (valid) log_write(HOST_LOG_INFO, "system arguments ok", 19);
    else log_write(HOST_LOG_ERROR, "system arguments failed", 23);
}
