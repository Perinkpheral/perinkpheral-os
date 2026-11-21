/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "question_press.h"
#include "tinytest.h"

static void test_other_buttons_preserve_ok(void)
{
    const scene_button_t others[] = { SCENE_BUTTON_UP, SCENE_BUTTON_DOWN, SCENE_BUTTON_BACK };
    for (unsigned i = 0; i < sizeof others / sizeof others[0]; i++) {
        scene_t question = { .question = 1 };
        question_press_t state = {0};
        question_press_shown(&state, &question, 100);
        CHECK(!question_press_accepts(&state, &question, SCENE_BUTTON_OK, SCENE_PRESS_DOWN, 101));
        CHECK(question_press_accepts(&state, &question, others[i], SCENE_PRESS_DOWN, 102));
        CHECK(question_press_accepts(&state, &question, others[i], SCENE_PRESS_UP, 103));
        CHECK(question_press_accepts(&state, &question, others[i], SCENE_PRESS_SHORT, 103));
        CHECK(question_press_accepts(&state, &question, SCENE_BUTTON_OK, SCENE_PRESS_UP, 104));
        CHECK(question_press_accepts(&state, &question, SCENE_BUTTON_OK, SCENE_PRESS_SHORT, 104));
        CHECK(!question_press_accepts(&state, &question, SCENE_BUTTON_OK, SCENE_PRESS_SHORT, 105));
    }
}

static void test_only_the_displayed_question_can_be_answered(void)
{
    scene_t question = { .question = 1 }, other = { .question = 1 };
    question_press_t state = {0};
    question_press_shown(&state, &other, 100);
    CHECK(!question_press_accepts(&state, &question, SCENE_BUTTON_OK, SCENE_PRESS_DOWN, 101));
    question_press_shown(&state, &question, 102);
    CHECK(!question_press_accepts(&state, &question, SCENE_BUTTON_OK, SCENE_PRESS_SHORT, 103));
    CHECK(!question_press_accepts(&state, &question, SCENE_BUTTON_OK, SCENE_PRESS_DOWN, 101));
    CHECK(!question_press_accepts(&state, &question, SCENE_BUTTON_OK, SCENE_PRESS_SHORT, 103));

    CHECK(!question_press_accepts(&state, &question, SCENE_BUTTON_OK, SCENE_PRESS_DOWN, 104));
    question.question++;
    question_press_shown(&state, &question, 105);
    CHECK(!question_press_accepts(&state, &question, SCENE_BUTTON_OK, SCENE_PRESS_UP, 106));
    CHECK(!question_press_accepts(&state, &question, SCENE_BUTTON_OK, SCENE_PRESS_SHORT, 106));
    CHECK(!question_press_accepts(&state, &question, SCENE_BUTTON_OK, SCENE_PRESS_DOWN, 107));
    CHECK(question_press_accepts(&state, &question, SCENE_BUTTON_OK, SCENE_PRESS_LONG, 108));
    CHECK(!question_press_accepts(&state, &question, SCENE_BUTTON_OK, SCENE_PRESS_UP, 109));
}

static void test_native_question_tracks_each_button(void)
{
    scene_t question = { .asks_once = true };
    question_press_t state = {0};
    question_press_shown(&state, &question, 100);
    CHECK(!question_press_accepts(&state, &question, SCENE_BUTTON_OK, SCENE_PRESS_DOWN, 101));
    CHECK(!question_press_accepts(&state, &question, SCENE_BUTTON_BACK, SCENE_PRESS_DOWN, 102));
    CHECK(question_press_accepts(&state, &question, SCENE_BUTTON_BACK, SCENE_PRESS_SHORT, 103));
    CHECK(question_press_accepts(&state, &question, SCENE_BUTTON_OK, SCENE_PRESS_SHORT, 104));
}

int main(void)
{
    test_other_buttons_preserve_ok();
    test_only_the_displayed_question_can_be_answered();
    test_native_question_tracks_each_button();
    REPORT();
}
