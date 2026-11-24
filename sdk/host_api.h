/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * Perinkpheral OS API: the contract between Perinkpheral OS firmware and wasm
 * applications. Applications include this header and link the imports.
 * Perinkpheral OS builds its import table from the same names.
 *
 * ABI 3 first appeared in firmware 0.1.30. Approved changes before the
 * first public release removed scene_state_set and scene_state_get and
 * increased session capacity in 0.1.61, and changed network-forgetting
 * behavior in 0.1.66. After the public release, existing names, signatures,
 * privileges, layouts, values and meanings must remain compatible;
 * incompatible changes require a new ABI. Additions after the 0.1.30 floor
 * carry valid HOST_SINCE_<name> introduction stamps before snapshot recording;
 * established baseline entries need none and recorded stamps stay fixed.
 * Only host_answer_info_t may append fields beyond its old extent, preserving
 * alignment and every existing field type, offset and size. Other recorded
 * layouts stay fixed. ABI 2 is unsupported.
 * Commands use subject then verb (gfx_draw_text); queries name what is
 * asked (gfx_width); events called by Perinkpheral OS are app_on_<what happened>.
 *
 * The contract is written by subject in host/, and this header includes all
 * of it. An application includes this header and no file of host/: which
 * declaration stands in which of them is not part of the contract and may
 * change. The tools read the headers in the order they are included here.
 */
#pragma once

#include "host/base.h"
#include "host/app.h"
#include "host/buttons.h"
#include "host/text.h"
#include "host/gfx.h"
#include "host/ui.h"
#include "host/screen.h"
#include "host/net.h"
#include "host/store.h"
#include "host/time.h"
#include "host/sys.h"
