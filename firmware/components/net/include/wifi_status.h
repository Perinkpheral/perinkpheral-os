/* SPDX-License-Identifier: MIT */
#pragma once

#include "wifi.h"

/* Publish the plan and retained connection result under the caller's lock.
 * Returns whether the header's connection state may have changed. */
/* A failed attempt or lost connection while the radio is running. */
void wifi_status_lost(wifi_status_t *status);

bool wifi_status_update(wifi_status_t *status, wifi_phase_t phase, bool radio_off);
