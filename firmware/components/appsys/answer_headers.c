/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe */
#include "answer_headers.h"
#include "response_format.h"
#include "host_api.h"
#include <string.h>
#include <strings.h>
#include <stdlib.h>

void answer_headers_type(answer_headers_t *headers, const char *type)
{
    int format = response_format(0, type);
    if (headers->type_seen && headers->mime_format != format)
        format = HOST_FORMAT_RAW;
    headers->type_seen = true;
    headers->mime_format = (uint8_t)format;
}
int answer_format(int policy, const answer_headers_t *headers, const host_answer_info_t *info)
{
    if (policy || !headers || !headers->type_seen)
        return response_format(policy, info ? info->content_type : NULL);
    return headers->mime_format;
}
void answer_headers_reset(answer_headers_t *headers)
{
    memset(headers, 0, sizeof *headers);
}

bool answer_header_name_valid(const char *name, size_t length)
{
    if (name == NULL || length == 0 || length > ANSWER_HEADER_NAME_MAX) {
        return false;
    }
    for (size_t i = 0; i < length; i++) {
        unsigned char letter = (unsigned char)name[i];
        bool alphanumeric = (letter >= 'a' && letter <= 'z') ||
                            (letter >= 'A' && letter <= 'Z') ||
                            (letter >= '0' && letter <= '9');
        if (!alphanumeric && (letter == 0 || strchr("!#$%&'*+-.^_`|~", letter) == NULL)) {
            return false;
        }
    }
    return true;
}

void answer_headers_add(answer_headers_t *headers, const char *name, const char *value)
{
    if (headers && name && value && !strcasecmp(name, "Content-Type"))
        answer_headers_type(headers, value);
    if (headers->overflow) {
        return;
    }
    size_t name_length = strnlen(name, ANSWER_HEADER_NAME_MAX + 1);
    size_t available = sizeof headers->text - headers->used;
    if (!answer_header_name_valid(name, name_length) || name_length + 2 > available) {
        headers->overflow = true;
        return;
    }
    size_t value_length = strnlen(value, available - name_length - 1);
    if (value_length > available - name_length - 2) {
        /* A partial header set cannot safely describe an authentication response. */
        headers->overflow = true;
        return;
    }
    memcpy(headers->text + headers->used, name, name_length + 1);
    headers->used += (uint16_t)(name_length + 1);
    memcpy(headers->text + headers->used, value, value_length + 1);
    headers->used += (uint16_t)(value_length + 1);
}

static unsigned char lowercase(unsigned char letter)
{
    return letter >= 'A' && letter <= 'Z' ? letter + ('a' - 'A') : letter;
}

static bool same_name(const char *saved, const char *name, size_t length)
{
    if (strlen(saved) != length) {
        return false;
    }
    for (size_t i = 0; i < length; i++) {
        if (lowercase((unsigned char)saved[i]) != lowercase((unsigned char)name[i])) {
            return false;
        }
    }
    return true;
}

int32_t answer_headers_get(const answer_headers_t *headers, const char *name, size_t name_length,
                           int32_t occurrence, char *value, int32_t capacity)
{
    if (!answer_header_name_valid(name, name_length) || occurrence < 0 || capacity < 0 ||
        (capacity > 0 && value == NULL)) {
        return HOST_ERR_ARGUMENT;
    }
    if (headers == NULL) {
        return HOST_ERR_NOT_FOUND;
    }
    if (headers->overflow) {
        return HOST_ERR_NO_SPACE;
    }
    size_t offset = 0;
    while (offset < headers->used) {
        const char *saved_name = headers->text + offset;
        offset += strlen(saved_name) + 1;
        const char *saved_value = headers->text + offset;
        size_t length = strlen(saved_value);
        offset += length + 1;
        if (same_name(saved_name, name, name_length) && occurrence-- == 0) {
            if (length > (size_t)capacity) {
                return HOST_ERR_NO_SPACE;
            }
            if (length > 0) {
                memcpy(value, saved_value, length);
            }
            return (int32_t)length;
        }
    }
    return HOST_ERR_NOT_FOUND;
}

/* IMF-fixdate is deliberately parsed without locale or the process time zone. */
static int decimal_digits(const char *text, size_t length)
{
    int value = 0;
    for (size_t index = 0; index < length; index++) {
        if (text[index] < '0' || text[index] > '9') {
            return -1;
        }
        value = value * 10 + text[index] - '0';
    }
    return value;
}

static int64_t http_date(const char *text, size_t length)
{
    static const char *const WEEKDAYS[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
    static const char *const MONTHS[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    static const int MONTH_DAYS[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (length != 29 || memcmp(text + 3, ", ", 2) != 0 || text[7] != ' ' ||
        text[11] != ' ' || text[16] != ' ' || text[19] != ':' || text[22] != ':' ||
        memcmp(text + 25, " GMT", 4) != 0) {
        return -1;
    }
    bool weekday_valid = false;
    for (size_t index = 0; index < sizeof WEEKDAYS / sizeof WEEKDAYS[0]; index++) {
        weekday_valid = weekday_valid || memcmp(text, WEEKDAYS[index], 3) == 0;
    }
    int month = 0;
    while (month < 12 && memcmp(text + 8, MONTHS[month], 3) != 0) {
        month++;
    }
    int day = decimal_digits(text + 5, 2);
    int year = decimal_digits(text + 12, 4);
    int hour = decimal_digits(text + 17, 2);
    int minute = decimal_digits(text + 20, 2);
    int second = decimal_digits(text + 23, 2);
    bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
    if (!weekday_valid || month == 12 || year < 1601 || day < 1 ||
        day > MONTH_DAYS[month] + (month == 1 && leap) ||
        hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 59) {
        return -1;
    }
    int previous_year = year - 1;
    /* Gregorian days before this year, relative to 1970-01-01. */
    int64_t days = (int64_t)previous_year * 365 + previous_year / 4 - previous_year / 100 +
                   previous_year / 400 - 719162;
    for (int index = 0; index < month; index++) {
        days += MONTH_DAYS[index] + (index == 1 && leap);
    }
    return ((days + day - 1) * 24 + hour) * 3600 + minute * 60 + second;
}

int32_t answer_retry_after(const char *value, int64_t unix_time)
{
    if (value == NULL) {
        return 0;
    }
    while (*value == ' ' || *value == '\t') {
        value++;
    }
    size_t length = strlen(value);
    while (length > 0 && (value[length - 1] == ' ' || value[length - 1] == '\t')) {
        length--;
    }
    if (length == 0) {
        return 0;
    }
    if (value[0] >= '0' && value[0] <= '9') {
        int32_t seconds = 0;
        for (size_t index = 0; index < length; index++) {
            if (value[index] < '0' || value[index] > '9') {
                return 0;
            }
            int digit = value[index] - '0';
            seconds = seconds > (INT32_MAX - digit) / 10 ? INT32_MAX : seconds * 10 + digit;
        }
        return seconds;
    }
    if (unix_time < HOST_TIME_MIN || unix_time > HOST_TIME_MAX) {
        return 0;
    }
    int64_t date = http_date(value, length);
    int64_t seconds = date - unix_time;
    return seconds <= 0 ? 0 : seconds > INT32_MAX ? INT32_MAX : (int32_t)seconds;
}

int32_t answer_poll_cooldown(const host_answer_info_t *response)
{
    enum { HTTP_TOO_MANY_REQUESTS = 429, DEFAULT_COOLDOWN_S = 60 };
    return response->retry_after > 0 ? response->retry_after :
           response->status == HTTP_TOO_MANY_REQUESTS ? DEFAULT_COOLDOWN_S : 0;
}

/* The headers an application may ask about, and where each goes. */
void answer_info_header(host_answer_info_t *response, const char *name, const char *value,
                        int64_t unix_time)
{
    const struct {
        const char *name;
        char       *text;
        size_t      size;
    } KEPT[] = {
        { "Content-Type", response->content_type, sizeof response->content_type },
        { "ETag", response->etag, sizeof response->etag },
        { "Last-Modified", response->last_modified, sizeof response->last_modified },
        { "Location", response->location, sizeof response->location },
        { "Link", response->link, sizeof response->link },
    };
    for (size_t i = 0; i < sizeof KEPT / sizeof KEPT[0]; i++) {
        if (strcasecmp(name, KEPT[i].name) == 0) {
            size_t length = strlen(value);
            if (length >= KEPT[i].size) {
                length = KEPT[i].size - 1;
            }
            memcpy(KEPT[i].text, value, length);
            KEPT[i].text[length] = '\0';
            return;
        }
    }
    if (strcasecmp(name, "Retry-After") == 0) {
        response->retry_after = answer_retry_after(value, unix_time);
    }
}
