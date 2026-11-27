/* SPDX-License-Identifier: MIT */
#pragma once
#define SNTP_SYNC_STATUS_COMPLETED 1
#define ESP_SNTP_OPMODE_POLL 0
void sntp_set_sync_status(int status);
void esp_sntp_init(void);
void esp_sntp_stop(void);
void esp_sntp_setoperatingmode(int mode);
void esp_sntp_setservername(int index, char *name);
void esp_sntp_restart(void);
