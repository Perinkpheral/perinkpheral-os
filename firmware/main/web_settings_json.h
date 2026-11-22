/* SPDX-License-Identifier: MIT */
#pragma once

#include "appconf.h"
#include "json_tree.h"

/* NULL if any member cannot be built; a partial response could overwrite settings.
 * The menu order is split in place. */
json_t *web_settings_json(const appconf_t *settings, char *order);

/* NULL rather than an incomplete list that could erase saved sources. */
json_t *web_sources_json(const char *sources);
