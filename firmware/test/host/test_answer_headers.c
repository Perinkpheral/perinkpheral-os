/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe */
#include "answer_headers.h"
#include "host_api.h"
#include "tinytest.h"

static void test_response_metadata(void)
{
    host_answer_info_t info = { .status = 200, .length = -1 };
    answer_info_header(&info, "cOnTeNt-TyPe", "application/json", 0);
    answer_info_header(&info, "ETag", "old", 0);
    answer_info_header(&info, "etag", "new", 0);
    answer_info_header(&info, "last-modified", "yesterday", 0);
    answer_info_header(&info, "Location", "next", 0);
    answer_info_header(&info, "Link", "<next>", 0);
    answer_info_header(&info, "Retry-After", "30", 0);
    CHECK_STR(info.content_type, "application/json");
    CHECK_STR(info.etag, "new");
    CHECK_STR(info.last_modified, "yesterday");
    CHECK_STR(info.location, "next");
    CHECK_STR(info.link, "<next>");
    CHECK(info.retry_after == 30 && info.status == 200 && info.length == -1);
    for (unsigned index = 0; index < 4; index++) {
        static const char *const ABSENT[] = { "Wed, 21 Oct 2030", "-1", "0", "2147483647x" };
        answer_info_header(&info, "retry-after", ABSENT[index], 0);
        CHECK(info.retry_after == 0);
    }
    char large[128];
    memset(large, 'x', sizeof large - 1);
    large[sizeof large - 1] = '\0';
    answer_info_header(&info, "ETag", large, 0);
    CHECK(strlen(info.etag) == sizeof info.etag - 1);
    CHECK(info.etag[sizeof info.etag - 2] == 'x');
}

static void test_retry_after_normalization(void)
{
    const int64_t now = 1795780800; /* 2026-11-27 12:00:00 UTC */
    CHECK(answer_retry_after("30", 0) == 30);
    CHECK(answer_retry_after(" \t00030\t ", now) == 30);
    CHECK(answer_retry_after("2147483647", now) == INT32_MAX);
    CHECK(answer_retry_after("2147483648", now) == INT32_MAX);
    CHECK(answer_retry_after("99999999999999999999999999999999", 0) == INT32_MAX);
    const char *invalid[] = { "", " ", "-1", "+1", "30junk", "1.5", "1e2", "30\n", "30 0",
        "99999999999999999999x", "Fri, 27 Nov 2026 12:01:00 UTC", "Fri, 31 Nov 2026 12:01:00 GMT",
        "Fri, 27 Nov 2026 24:01:00 GMT", "Fri, 27 Nov 2026 12:01:60 GMT", "Fri, 29 Feb 2027 12:00:00 GMT" };
    for (size_t index = 0; index < sizeof invalid / sizeof invalid[0]; index++) {
        CHECK(answer_retry_after(invalid[index], now) == 0);
    }
    CHECK(answer_retry_after("Fri, 27 Nov 2026 12:01:00 GMT", now) == 60);
    CHECK(answer_retry_after("Fri, 27 Nov 2026 12:01:00 GMT", 0) == 0);
    CHECK(answer_retry_after("Fri, 27 Nov 2026 12:00:00 GMT", now) == 0);
    CHECK(answer_retry_after("Fri, 27 Nov 2026 11:59:59 GMT", now) == 0);
    CHECK(answer_retry_after("Fri, 31 Dec 9999 23:59:59 GMT", now) == INT32_MAX);
    CHECK(answer_retry_after("Tue, 29 Feb 2028 12:00:00 GMT", now) == 459 * 86400);
    host_answer_info_t info = { .status = 429 };
    CHECK(answer_poll_cooldown(&info) == 60);
    info.retry_after = 120;
    CHECK(answer_poll_cooldown(&info) == 120);
    info.status = 503;
    CHECK(answer_poll_cooldown(&info) == 120);
    info.retry_after = 0;
    CHECK(answer_poll_cooldown(&info) == 0);
}

int main(void)
{
    test_response_metadata();
    test_retry_after_normalization();
    answer_headers_t headers;
    answer_headers_reset(&headers);
    answer_headers_add(&headers, "Set-Cookie", "session=one; Path=/; HttpOnly");
    answer_headers_add(&headers, "X-NDM-Realm", "router");
    answer_headers_add(&headers, "set-cookie", "other=two; Expires=Wed, 21 Oct 2030 07:28:00 GMT");
    answer_headers_add(&headers, "X-NDM-Challenge", "0123456789abcdef");
    answer_headers_add(&headers, "Empty", "");
    char value[128] = "unchanged";
    CHECK(answer_headers_get(&headers, "SET-cookie", 10, 0, value, sizeof value) == 29);
    CHECK(memcmp(value, "session=one; Path=/; HttpOnly", 29) == 0);
    CHECK(answer_headers_get(&headers, "set-cookie", 10, 1, value, sizeof value) == 48);
    CHECK(memcmp(value, "other=two; Expires=Wed, 21 Oct 2030 07:28:00 GMT", 48) == 0);
    strcpy(value, "unchanged");
    CHECK(answer_headers_get(&headers, "X-NDM-Realm", 11, 0, value, 5) == HOST_ERR_NO_SPACE);
    CHECK_STR(value, "unchanged");
    CHECK(answer_headers_get(&headers, "missing", 7, 0, value, sizeof value) == HOST_ERR_NOT_FOUND);
    CHECK(answer_headers_get(&headers, "set-cookie", 10, 2, value, sizeof value) == HOST_ERR_NOT_FOUND);
    CHECK(answer_headers_get(&headers, "Empty", 5, 0, NULL, 0) == 0);
    CHECK(answer_headers_get(&headers, "Bad Name", 8, 0, value, sizeof value) == HOST_ERR_ARGUMENT);
    CHECK(answer_headers_get(&headers, "a:b", 3, 0, value, sizeof value) == HOST_ERR_ARGUMENT);
    CHECK(answer_headers_get(&headers, "a\0b", 3, 0, value, sizeof value) == HOST_ERR_ARGUMENT);
    CHECK(answer_headers_get(&headers, "x", 1, -1, value, sizeof value) == HOST_ERR_ARGUMENT);
    CHECK(answer_headers_get(&headers, "x", 1, 0, value, -1) == HOST_ERR_ARGUMENT);
    CHECK(answer_headers_get(&headers, "x", 1, 0, NULL, 1) == HOST_ERR_ARGUMENT);
    CHECK(answer_header_name_valid("!#$%&'*+-.^_`|~09Az", 18));
    char too_large[ANSWER_HEADERS_BYTES + 1];
    memset(too_large, 'x', sizeof too_large - 1);
    too_large[sizeof too_large - 1] = 0;
    answer_headers_add(&headers, "Set-Cookie", too_large);
    answer_headers_add(&headers, "Set-Cookie", "later=must-not-be-number-two");
    CHECK(headers.overflow);
    CHECK(answer_headers_get(&headers, "set-cookie", 10, 2, value, sizeof value) == HOST_ERR_NO_SPACE);
    CHECK(answer_headers_get(&headers, "not-sent", 8, 0, value, sizeof value) == HOST_ERR_NO_SPACE);
    CHECK(answer_headers_get(&headers, "X-NDM-Realm", 11, 0, value, sizeof value) == HOST_ERR_NO_SPACE);
    CHECK(answer_headers_get(&headers, "set-cookie", 10, 0, value, sizeof value) == HOST_ERR_NO_SPACE);
    /* Redirects start a new response, including on a reused connection. */
    answer_headers_reset(&headers);
    CHECK(answer_headers_get(&headers, "set-cookie", 10, 0, value, sizeof value) == HOST_ERR_NOT_FOUND);
    CHECK(!headers.overflow && headers.used == 0);
    for (unsigned i = 0; i < sizeof headers.text; i++) { CHECK(headers.text[i] == 0); }
    answer_headers_add(&headers, "X-New", "yes");
    CHECK(answer_headers_get(&headers, "x-new", 5, 0, value, sizeof value) == 3);
    answer_headers_t reversed;
    answer_headers_reset(&reversed);
    answer_headers_add(&reversed, "A-Filler", too_large);
    answer_headers_add(&reversed, "X-NDM-Realm", "router");
    CHECK(answer_headers_get(&reversed, "X-NDM-Realm", 11, 0, value, sizeof value) == HOST_ERR_NO_SPACE);
    /* Exact capacity stores a whole field; the next field fails closed. */
    answer_headers_reset(&headers);
    too_large[ANSWER_HEADERS_BYTES - 3] = 0;
    answer_headers_add(&headers, "x", too_large);
    CHECK(headers.used == ANSWER_HEADERS_BYTES && !headers.overflow);
    answer_headers_add(&headers, "y", "");
    CHECK(headers.overflow);
    CHECK(sizeof headers == 1028);
    REPORT();
}
