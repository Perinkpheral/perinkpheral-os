/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "memory.h"
#include "deadlines.h"
#include "host.h"
#include "units.h"
#include "glyphs.h"
#include "tinytest.h"

#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"

static _Thread_local bool failure_lock_held;
static pthread_mutex_t failure_lock = PTHREAD_MUTEX_INITIALIZER;
void memory_test_enter(portMUX_TYPE *lock)
{
    (void)lock;
    pthread_mutex_lock(&failure_lock);
    failure_lock_held = true;
}
void memory_test_exit(portMUX_TYPE *lock)
{
    (void)lock;
    failure_lock_held = false;
    pthread_mutex_unlock(&failure_lock);
}

static char warning[512];
void memory_test_log(const char *format, ...)
{
    CHECK(!failure_lock_held);
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(warning, sizeof warning, format, arguments);
    va_end(arguments);
}

static uint32_t free_bytes = 100 * KB;
static size_t block_bytes = 64 * KB;
static int64_t now_us;
static int restarts;
static int sheds;
static int looks_scheduled;
static void (*failed_allocation)(size_t, uint32_t, const char *);

uint32_t esp_get_free_heap_size(void) { return free_bytes; }
size_t heap_caps_get_largest_free_block(uint32_t caps)
{
    (void)caps;
    return block_bytes;
}
int64_t esp_timer_get_time(void) { return now_us; }
int heap_caps_register_failed_alloc_callback(void (*callback)(size_t, uint32_t, const char *))
{
    failed_allocation = callback;
    return 0;
}
bool deadlines_set_in(deadline_kind_t kind, uint32_t owner, int number, int64_t in_ms)
{
    CHECK(kind == DEADLINE_MEMORY_LOOK && owner == 0 && number == 0 && in_ms == 5000);
    looks_scheduled++;
    return true;
}
void host_stop_soon(host_stop_t why)
{
    CHECK(why == HOST_STOP_RESTART);
    restarts++;
}
static void shed(void) { sheds++; }
static void at_second(int64_t seconds) { now_us = seconds * US_PER_SECOND; }

static void test_glyph_admission(void)
{
    memory_counts_t before, after;
    memory_read(&before);
    const size_t sizes[] = { 1, 4 * KB, GLYPHS_PACK_MAX_BYTES };
    for (size_t index = 0; index < sizeof sizes / sizeof sizes[0]; index++) {
        size_t pack_bytes = sizes[index];
        free_bytes = pack_bytes + GLYPHS_COMPACTION_BYTES + 24 * KB;
        block_bytes = pack_bytes + GLYPHS_COMPACTION_BYTES + 8 * KB;
        CHECK(memory_has_glyph_pack(pack_bytes));
        free_bytes--;
        CHECK(!memory_has_glyph_pack(pack_bytes));
        free_bytes++;
        block_bytes--;
        CHECK(!memory_has_glyph_pack(pack_bytes));
    }
    free_bytes = 24 * KB;
    block_bytes = 8 * KB;
    CHECK(!memory_has_glyph_pack(GLYPHS_PACK_MAX_BYTES));
    free_bytes = 100 * KB;
    block_bytes = 64 * KB;
    CHECK(!memory_has_glyph_pack(0));
    CHECK(!memory_has_glyph_pack(GLYPHS_PACK_MAX_BYTES + 1));
    CHECK(!memory_has_glyph_pack(SIZE_MAX)); /* no overflow to a tiny requirement */
    CHECK(memory_has(MEMORY_FOR_A_GLYPH_PACK));
    memory_read(&after);
    CHECK(after.refusals == before.refusals + 10);
}

static void test_peeking(void)
{
    /* A look agrees with the counted question and is not itself a refusal. */
    memory_counts_t before, after;
    memory_read(&before);
    free_bytes = 50 * KB;
    block_bytes = 17 * KB;
    CHECK(!memory_peek(MEMORY_FOR_A_SESSION));
    CHECK(!memory_peek(MEMORY_FOR_A_SESSION));
    free_bytes = 60 * KB;
    block_bytes = 17 * KB;
    CHECK(memory_peek(MEMORY_FOR_A_SESSION));
    free_bytes = 100 * KB;
    block_bytes = 64 * KB;
    CHECK(memory_peek(MEMORY_FOR_A_GLYPH_PACK));
    free_bytes = 50 * KB;
    CHECK(!memory_peek(MEMORY_FOR_A_GLYPH_PACK));
    memory_read(&after);
    CHECK(after.refusals == before.refusals);
    /* Room seen by a look ends a refused period: no restart ten minutes on. */
    int restarts_before = restarts;
    free_bytes = 50 * KB;
    block_bytes = 17 * KB;
    CHECK(!memory_has(MEMORY_FOR_A_SESSION));
    free_bytes = 60 * KB;
    CHECK(memory_peek(MEMORY_FOR_A_SESSION));
    free_bytes = 50 * KB; /* a session of the poller's own, when the look comes */
    at_second(100000);
    memory_look();
    CHECK(restarts == restarts_before);
    memory_read(&before);
    /* The counted question still counts, once, on the same short heap. */
    CHECK(!memory_has(MEMORY_FOR_A_SESSION));
    memory_read(&after);
    CHECK(after.refusals == before.refusals + 1);
}

static void *refuse_glyph_packs(void *unused)
{
    (void)unused;
    for (int index = 0; index < 10000; index++)
        memory_has_glyph_pack(0);
    return NULL;
}

static void test_concurrent_refusals(void)
{
    memory_counts_t before, after;
    memory_read(&before);
    pthread_t workers[4];
    for (int index = 0; index < 4; index++)
        CHECK(pthread_create(&workers[index], NULL, refuse_glyph_packs, NULL) == 0);
    for (int index = 0; index < 4; index++)
        CHECK(pthread_join(workers[index], NULL) == 0);
    memory_read(&after);
    CHECK(after.refusals == before.refusals + 40000);
}

static void test_failure_details(void)
{
    free_bytes = 100 * KB;
    block_bytes = 64 * KB;
    memory_look();
    failed_allocation(12345, 0x5a, "allocation_probe");
    memory_look();
    CHECK_STR(warning, "the last one: 12345 bytes, caps 0x5a, allocation_probe");
    memory_look();
    failed_allocation(6789, 0xc3, NULL);
    memory_look();
    CHECK_STR(warning, "the last one: 6789 bytes, caps 0xc3, ?");
    memory_counts_t counts;
    memory_read(&counts);
    CHECK(counts.failed_allocations == 3);
}

static void *fail_allocations(void *context)
{
    bool second = context != NULL;
    for (int index = 0; index < 10000; index++)
        failed_allocation(second ? 2222 : 1111, second ? 0xbb : 0xaa, second ? "second" : "first");
    return NULL;
}

static void test_concurrent_failure_details(void)
{
    memory_counts_t before, after;
    memory_read(&before);
    pthread_t first, second;
    bool second_context = true;
    CHECK(pthread_create(&first, NULL, fail_allocations, NULL) == 0);
    CHECK(pthread_create(&second, NULL, fail_allocations, &second_context) == 0);
    for (int look = 0; look < 1000; look++) {
        warning[0] = '\0';
        memory_look();
        if (strncmp(warning, "the last one:", 13) == 0) {
            CHECK(strcmp(warning, "the last one: 1111 bytes, caps 0xaa, first") == 0 ||
                  strcmp(warning, "the last one: 2222 bytes, caps 0xbb, second") == 0);
        }
    }
    CHECK(pthread_join(first, NULL) == 0 && pthread_join(second, NULL) == 0);
    memory_read(&after);
    CHECK(after.failed_allocations == before.failed_allocations + 20000);
}

int main(void)
{
    memory_start();
    memory_on_pressure(shed);
    CHECK(failed_allocation != NULL && looks_scheduled == 1);
    /* One transient refusal, then no new request before the ten-minute look. */
    at_second(10);
    free_bytes = 50 * KB;
    CHECK(!memory_has(MEMORY_FOR_A_SESSION));
    at_second(610);
    free_bytes = 100 * KB;
    memory_look();
    CHECK(restarts == 0 && sheds == 0);
    memory_counts_t counts;
    memory_read(&counts);
    CHECK(counts.refusals == 1);

    /* Recovery seen between refusals starts the next timeout anew. */
    at_second(700);
    block_bytes = 16 * KB;
    CHECK(!memory_has(MEMORY_FOR_A_SESSION));
    at_second(1000);
    block_bytes = 17 * KB;
    free_bytes = 60 * KB;
    memory_look();
    at_second(1100);
    block_bytes = 16 * KB;
    CHECK(!memory_has(MEMORY_FOR_A_SESSION));
    at_second(1300);
    memory_look();
    CHECK(restarts == 0);
    at_second(1699);
    memory_look();
    CHECK(restarts == 0);
    at_second(1700);
    memory_look();
    CHECK(restarts == 1); /* Still fragmented for a whole ten minutes. */

    /* Enough contiguous space alone is not recovery when total memory is short. */
    free_bytes = 100 * KB;
    block_bytes = 64 * KB;
    CHECK(memory_has(MEMORY_FOR_A_SESSION));
    at_second(1800);
    free_bytes = 59 * KB;
    CHECK(!memory_has(MEMORY_FOR_A_SESSION));
    at_second(2400);
    memory_look();
    CHECK(restarts == 2);

    /* Critical-memory shedding and restart still work independently. */
    free_bytes = 100 * KB;
    CHECK(memory_has(MEMORY_FOR_A_SESSION));
    memory_look();
    failed_allocation(16, 0, "test");
    memory_look();
    CHECK(sheds == 1 && restarts == 2);
    memory_look();
    free_bytes = 11 * KB;
    memory_look();
    memory_look();
    CHECK(sheds == 2 && restarts == 2);
    memory_look();
    CHECK(restarts == 3);
    memory_read(&counts);
    CHECK(counts.failed_allocations == 1 && counts.sheds == 2 && counts.refusals == 4);
    test_glyph_admission();
    test_peeking();
    test_failure_details();
    test_concurrent_refusals();
    test_concurrent_failure_details();
    REPORT();
}
