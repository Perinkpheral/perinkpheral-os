/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdint.h>

#include "scene.h"

typedef struct {
    const scene_t *volatile shown_scene;
    uint32_t shown_question;
    int64_t shown_at_ms;
    uint8_t went_down;
    struct {
        const scene_t *scene;
        uint32_t question;
    } began_on[SCENE_BUTTON_COUNT];
} question_press_t;

/* Called only after the display accepts the frame, including an unchanged frame. */
void question_press_shown(question_press_t *state, const scene_t *scene, int64_t at_ms);

/* Irreversible presses belong only to the question visible when the button went down. */
bool question_press_accepts(question_press_t *state, const scene_t *scene, scene_button_t button,
                            scene_press_t press, int64_t at_ms);
