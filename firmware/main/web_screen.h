/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include "web_util.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The picture of the screen, in every build:
 *
 *   GET /api/screen    the frame the panel shows, a 1-bit BMP in the project's ink and paper;
 *                      X-Frame counts refreshes, X-Pending is 1 while a newer frame waits for
 *                      the panel. 503 when there is no memory for a copy of the frame.
 *
 * The page saves it as a screenshot; tools/perinkpheral shot and the simulator use the same
 * route. */
extern const web_route_t WEB_SCREEN_ROUTES[];
extern const size_t      WEB_SCREEN_ROUTE_COUNT;

#ifdef __cplusplus
}
#endif
