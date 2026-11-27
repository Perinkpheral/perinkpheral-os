/* SPDX-License-Identifier: MIT */
#include "wifi_status.h"

void wifi_status_lost(wifi_status_t *status)
{
    status->connection_known = false;
}

bool wifi_status_update(wifi_status_t *status, wifi_phase_t phase, bool radio_off)
{
    bool network_up = !radio_off && phase == WIFI_PHASE_ONLINE && !status->connection_known;
    if (network_up) {
        status->connection_known = true;
        status->connection_generation++;
    }
    wifi_phase_t published_phase = radio_off ? WIFI_PHASE_CONNECTING : phase;
    /* The plan keeps its phase during intentional shutdown. A new wake resets
     * it, so an attempt without an address cannot retain an earlier success. */
    bool was_reached = phase == WIFI_PHASE_ONLINE;
    bool changed = network_up || status->phase != published_phase || status->was_reached != was_reached ||
                   status->is_radio_off != radio_off;
    status->phase = published_phase;
    status->was_reached = was_reached;
    status->is_radio_off = radio_off;
    if (status->phase != WIFI_PHASE_ONLINE) {
        status->address[0] = '\0';
    }
    return changed;
}
