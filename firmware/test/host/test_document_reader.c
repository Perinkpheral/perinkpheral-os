/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe */
#include "tinytest.h"
#include "document_reader.h"
#include "answer_headers.h"
#include <string.h>

int main(void)
{
    const char *mime[] = {"application/json",
                          "TEXT/XML; charset=utf-8",
                          "image/svg+xml",
                          "application/vnd.example+JSON",
                          "text/plain",
                          "application/yaml",
                          "",
                          NULL,
                          "text/ +xml",
                          "wrong//x+xml",
                          "any/+xml",
                          "audio/json"};
    int expected[] = {1, 2, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0};
    for (unsigned i = 0; i < sizeof expected / sizeof expected[0]; i++) {
        answer_headers_t h = {0};
        if (mime[i])
            answer_headers_add(&h, "CONTENT-Type", mime[i]);
        CHECK(answer_format(0, &h, NULL) == expected[i]);
        CHECK(answer_format(1, &h, NULL) == 1);
        CHECK(answer_format(2, &h, NULL) == 2);
        CHECK(answer_format(3, &h, NULL) == 0);
    }
    answer_headers_t h = {0};
    answer_headers_add(
        &h, "Content-Type",
        "application/vnd.this-is-a-very-long-format-tag-that-is-too-long-for-answer-info+xml");
    CHECK(answer_format(0, &h, NULL) == 2);
    answer_headers_add(&h, "Content-Type", "application/json");
    CHECK(answer_format(0, &h, NULL) == 0);
    document_reader_t r = {0};
    uint8_t buf[128] = {0};
    const uint8_t json[] = "{\"name\":true}";
    int n = (int)sizeof json - 1;
    CHECK(!document_reader_feed(&r, buf, 32, 4, 1, 200, json, 3, HOST_DATA_FIRST, false));
    CHECK(document_reader_feed(&r, buf, 32, 4, 0, HOST_FAILED_NO_ANSWER, NULL, 0, HOST_DATA_FAILED,
                               false));
    CHECK(r.status == HOST_FAILED_NO_ANSWER && r.format == 1 && !r.used && !r.active);
    CHECK(!document_reader_feed(&r, buf, 32, 4, 1, 200, json, n, HOST_DATA_LAST, false));
    CHECK(document_reader_feed(&r, buf, 32, 5, 1, 200, json, n, HOST_DATA_FIRST | HOST_DATA_LAST,
                               false));
    CHECK(r.status == 200 && r.used == n && !memcmp(buf, json, n));
    CHECK(document_reader_feed(&r, buf, 2, 6, 1, 200, json, n, HOST_DATA_FIRST | HOST_DATA_LAST,
                               false));
    CHECK(r.status == HOST_FAILED_TOO_LARGE && !r.used);
    CHECK(document_reader_feed(&r, NULL, 0, 6, 2, 200, json, n, HOST_DATA_FIRST | HOST_DATA_LAST,
                               false));
    CHECK(r.status == HOST_FAILED_NOT_READY && !r.used);
    CHECK(!document_reader_feed(&r, NULL, 0, 9, 0, 200, json, n, HOST_DATA_FIRST | HOST_DATA_LAST,
                                false));
    CHECK(document_reader_feed(&r, NULL, 0, 9, 1, 204, NULL, 0, HOST_DATA_FIRST | HOST_DATA_LAST,
                               false));
    CHECK(r.status == 204 && !r.used);
    CHECK(document_reader_feed(&r, buf, 32, 9, 1, 403, json, n, HOST_DATA_FIRST | HOST_DATA_LAST,
                               false));
    CHECK(r.status == 403 && !r.used);
    CHECK(document_reader_feed(&r, buf, 32, 9, 1, 200, json, n - 1,
                               HOST_DATA_FIRST | HOST_DATA_LAST, false));
    CHECK(r.status == HOST_FAILED_NO_ANSWER && !r.used);
    CHECK(document_reader_feed(&r, buf, 32, 9, 2, 200, json, n, HOST_DATA_FIRST | HOST_DATA_LAST,
                               false));
    CHECK(r.status == HOST_FAILED_NO_ANSWER && !r.used);
    /* A document too large for its buffer is failed before the rest of its answer comes,
     * once: the rest of that answer, its failure among them, gives no second result. */
    CHECK(!document_reader_end_early(&r));
    CHECK(!document_reader_feed(&r, buf, 4, 10, 1, 200, json, n, HOST_DATA_FIRST, false));
    CHECK(r.active && r.error == HOST_FAILED_TOO_LARGE);
    CHECK(document_reader_end_early(&r));
    CHECK(r.status == HOST_FAILED_TOO_LARGE && !r.used && !r.active);
    CHECK(!document_reader_end_early(&r));
    CHECK(!document_reader_feed(&r, buf, 4, 10, 1, 200, json, n, 0, false));
    CHECK(!document_reader_feed(&r, buf, 4, 10, 0, HOST_FAILED_NO_ANSWER, NULL, 0,
                                HOST_DATA_FAILED, false));
    CHECK(r.status == HOST_FAILED_TOO_LARGE);
    /* The next answer of the same request, failing before its FIRST, is told. */
    CHECK(document_reader_feed(&r, buf, 4, 10, 1, HOST_FAILED_NO_NETWORK, NULL, 0,
                               HOST_DATA_FAILED, false));
    CHECK(r.status == HOST_FAILED_NO_NETWORK);
    /* A refusal's status is its whole result: given at its first piece. */
    CHECK(!document_reader_feed(&r, buf, 32, 12, 1, 404, json, 3, HOST_DATA_FIRST, false));
    CHECK(document_reader_end_early(&r) && r.status == 404 && !r.used && !r.active);
    CHECK(!document_reader_feed(&r, buf, 32, 12, 1, 404, json, 3, HOST_DATA_LAST, false));
    /* A document still being read is not ended. */
    CHECK(!document_reader_feed(&r, buf, 32, 11, 1, 200, json, 3, HOST_DATA_FIRST, false));
    CHECK(!document_reader_end_early(&r) && r.active);
    const char *invalid[] = {"",
                             "   ",
                             "[1 2]",
                             "{\"a\":1 \"b\":2}",
                             "true false",
                             "[1,]",
                             "{\"a\":1,}",
                             "01",
                             "-01",
                             "1.",
                             "1e+",
                             "+1",
                             "tRue",
                             "nul",
                             "[}",
                             "{\"k\" 1}",
                             "\"bad\\q\"",
                             "\"\\ud800\"",
                             "\"\\udc00\"",
                             "\"\\ud800\\u0041\"",
                             "\"\\u001z\"",
                             "\"raw\x01\"",
                             "\"\xc0\x80\"",
                             "\"\xed\xa0\x80\"",
                             "\"\xf4\x90\x80\x80\"",
                             "true\v"};
    for (unsigned i = 0; i < sizeof invalid / sizeof invalid[0]; i++)
        CHECK(!document_json_valid((const uint8_t *)invalid[i], strlen(invalid[i])));
    const char *valid[] = {"0",
                           "-0",
                           "-0.03e+1",
                           "null",
                           " [ true , false, null, {}, [] ]\n",
                           "{\"a\":[{\"b\":\"\\ud83d\\ude00\"}]}",
                           "\"a\\u0000b\"",
                           "\"тест 😎\""};
    for (unsigned i = 0; i < sizeof valid / sizeof valid[0]; i++)
        CHECK(document_json_valid((const uint8_t *)valid[i], strlen(valid[i])));
    uint8_t nested[130];
    memset(nested, '[', 65);
    memset(nested + 65, ']', 65);
    CHECK(document_json_valid(nested + 1, 128));
    CHECK(!document_json_valid(nested, 130));
    const char *bad_xml[] = {"<r><x untouched='&missing;'/></r>", "<r a='1' a='2'/>",
                             "<r><x a='1' a='2'/></r>"};
    for (unsigned i = 0; i < sizeof bad_xml / sizeof bad_xml[0]; i++) {
        CHECK(document_reader_feed(&r, buf, sizeof buf, 9, 2, 200, (const uint8_t *)bad_xml[i],
                                   strlen(bad_xml[i]), HOST_DATA_FIRST | HOST_DATA_LAST, false));
        CHECK(r.status == HOST_FAILED_NO_ANSWER && !r.used);
    }
    CHECK(document_reader_feed(&r, NULL, 0, 8, HOST_FORMAT_JSON, 200, NULL, 0,
                               HOST_DATA_FIRST | HOST_DATA_LAST, true));
    CHECK(r.status == 200 && !r.used);
    CHECK(document_reader_feed(&r, buf, sizeof buf, 8, HOST_FORMAT_JSON, 200, NULL, 0,
                               HOST_DATA_FIRST | HOST_DATA_LAST, false));
    CHECK(r.status == HOST_FAILED_NO_ANSWER && !r.used);
    CHECK(!document_reader_feed(&r, buf, sizeof buf, 8, 1, 200, json, 1, HOST_DATA_FIRST, false));
    CHECK(!document_reader_feed(&r, buf, sizeof buf, 9, 1, HOST_FAILED_NO_ANSWER, NULL, 0,
                                HOST_DATA_FAILED, false));
    CHECK(r.active && r.request == 8 && r.used == 1);
    CHECK(document_reader_feed(&r, buf, sizeof buf, 8, 1, 200, json + 1, n - 1, HOST_DATA_LAST,
                               false) &&
          r.status == 200);
    REPORT();
}
