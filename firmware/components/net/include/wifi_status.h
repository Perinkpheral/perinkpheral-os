/* SPDX-License-Identifier: MIT */
#pragma once

#include "wifi.h"

/* Publish the plan and retained connection result under the caller's lock.
 * Returns whether the header's connection state may have changed. */
bool wifi_status_update(wifi_status_t *status, wifi_phase_t phase, bool radio_off);
