/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * Perinkpheral OS API: the contract between Perinkpheral OS firmware and wasm
 * applications. Applications include this header and link the imports.
 * Perinkpheral OS builds its import table from the same names.
 *
 * ABI 3 starts with the first public release named by
 * HOST_ABI_3_FIRMWARE_FIRST. Earlier packages have no compatibility guarantee.
 * After that release, existing names, signatures, privileges, layouts,
 * values and meanings remain compatible; incompatible changes require a
 * new ABI. Later additions carry permanent HOST_SINCE_<name> introduction
 * stamps. Only the explicit admission and reservation limits listed in
 * ContractSnapshot.growableLimits may increase; applications using an
 * increased limit must declare its firmware minimum. Output sizes and
 * layout dimensions remain fixed. HOST_ICON_COUNT may grow with contiguous
 * appended icon IDs. Only host_answer_info_t may append fields beyond its
 * old extent, preserving alignment and every existing field type, offset
 * and size. Other recorded layouts stay fixed. ABI 2 is unsupported.
 *
 * A module's linear memory is fixed at build time, cannot grow and is at
 * most 64 KB (65536 bytes). ABI 3 reserves an application area of at least
 * 112 KB (114688 bytes), shared by linear memory, interpreter allocations
 * and contract scratch memory. Meeting the linear-memory limit does not
 * guarantee that a module fits this shared area. The builder's interpreter
 * estimate is advisory and does not establish peak use.
 * Surface sizes of 400 x 300 (full screen) and 400 x 260 (below the header),
 * and ink levels 0..16 are contract guarantees. Timing descriptions, font
 * metrics, dithering patterns and physical panel facts are informative,
 * not compatibility promises; measure text with the supplied calls.
 *
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
#include "host/xml.h"
