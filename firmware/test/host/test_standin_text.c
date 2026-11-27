/* SPDX-License-Identifier: MIT */
#include "tinytest.h"
#include "standin.h"

#include "text_cases.h"

const standin_subject_t standin_subject = { 0 };

int main(void)
{
    standin_reset();
    standin()->is_drawing = true;
    CHECK(text_contract_cases());
    standin_reset();
    const char log_bytes[] = {'A', '\n', 0, (char)0xc3, (char)0xa9, 'Z'};
    log_write(HOST_LOG_INFO, log_bytes, sizeof log_bytes);
    CHECK_STR(standin()->last_log, "A????Z");
    uint64_t sanitized_hash = standin()->diagnostics;
    standin_reset();
    log_write(HOST_LOG_INFO, "A????Z", 6);
    CHECK(standin()->diagnostics == sanitized_hash);
    char long_log[HOST_LOG_TEXT_MAX + 1];
    memset(long_log, 'x', sizeof long_log);
    log_write(HOST_LOG_INFO, long_log, sizeof long_log);
    CHECK(strlen(standin()->last_log) == HOST_LOG_TEXT_MAX);
    CHECK(standin()->last_log[HOST_LOG_TEXT_MAX - 1] == 'x');
    log_write(HOST_LOG_INFO, NULL, 1);
    log_write(HOST_LOG_INFO, "bad", -1);
    CHECK(standin()->logs == 2);
    standin()->is_drawing = true;
    const int32_t width = ui_text_width(HOST_UI_TEXT, "word", 4);
    const struct {
        const char *text;
        int32_t width, max_lines, lines;
    } cases[] = {
        { "wordwordword", width, 8, 3 },
        { "wordwordwordword word", width + 4, 8, 5 },
        { "word wordwordword", width, 8, 4 },
        { "   wordwordword   ", width, 8, 3 },
        { "wordwordword", width, 2, 2 },
        { "word word word", width + 4, 8, 3 },
        { "word word word", width * 2 + 40, 8, 2 },
        { "éééééééééééé", width, 8, 3 },
        { "ééé", 1, 8, 3 },
        { "€€€", 1, 8, 3 },
        { "😀😀😀", 1, 8, 3 },
        { "", width, 8, 0 },
        { "   ", width, 8, 0 },
    };
    for (size_t index = 0; index < sizeof cases / sizeof cases[0]; index++) {
        CHECK(ui_draw_text_lines(0, 0, HOST_UI_TEXT, cases[index].text,
                                (int32_t)strlen(cases[index].text), cases[index].width,
                                cases[index].max_lines) == cases[index].lines);
    }
    REPORT();
}
