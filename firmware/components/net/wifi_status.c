/* SPDX-License-Identifier: MIT */
#include "wifi_status.h"

bool wifi_status_update(wifi_status_t *status, wifi_phase_t phase, bool radio_off)
{
    wifi_phase_t published_phase = radio_off ? WIFI_PHASE_CONNECTING : phase;
    /* The plan keeps its phase during intentional shutdown. A new wake resets
     * it, so an attempt without an address cannot retain an earlier success. */
    bool was_reached = phase == WIFI_PHASE_ONLINE;
    bool changed = status->phase != published_phase || status->was_reached != was_reached ||
                   status->is_radio_off != radio_off;
    status->phase = published_phase;
    status->was_reached = was_reached;
    status->is_radio_off = radio_off;
    if (status->phase != WIFI_PHASE_ONLINE) {
        status->address[0] = '\0';
    }
    return changed;
}
