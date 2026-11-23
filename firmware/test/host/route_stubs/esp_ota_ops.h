/* SPDX-License-Identifier: MIT */
#pragma once
#include "esp_err.h"
typedef struct { char label[16]; } esp_partition_t;
const esp_partition_t *esp_ota_get_running_partition(void);
