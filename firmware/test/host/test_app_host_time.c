/* SPDX-License-Identifier: MIT */
#include "tinytest.h"
#include "deadline_table.h"
#include <time.h>

#define ESP_LOGD ESP_LOGI
#define ESP_LOGE ESP_LOGI
#include "../../main/app_host.c"

static int64_t wall_ms;
static int64_t boot_ms;
static deadline_table_t deadlines;
static const app_manifest_t application = { .id = 1 };

const app_manifest_t *launcher_find(const char *name)
{
    return strcmp(name, "test") == 0 ? &application : NULL;
}

int64_t esp_timer_get_time(void) { return boot_ms * US_PER_MS; }
int64_t deadlines_now_ms(void) { return wall_ms; }

bool deadlines_set_in(deadline_kind_t kind, uint32_t owner, int number, int64_t in_ms)
{
    return deadline_table_set(&deadlines, (deadline_id_t){ kind, owner, number }, wall_ms + in_ms, true);
}

bool deadlines_set_at(deadline_kind_t kind, uint32_t owner, int number, int64_t at_ms)
{
    return deadline_table_set(&deadlines, (deadline_id_t){ kind, owner, number }, at_ms, false);
}

void deadlines_cancel(deadline_kind_t kind, uint32_t owner, int number)
{
    deadline_table_cancel(&deadlines, (deadline_id_t){ kind, owner, number });
}

static void pass(int64_t milliseconds)
{
    wall_ms += milliseconds;
    boot_ms += milliseconds;
}

static void move_clock(int64_t milliseconds)
{
    wall_ms += milliseconds;
    deadline_table_clock_moved(&deadlines, milliseconds);
}

static int64_t alarm_left(int number)
{
    return deadline_table_when(&deadlines, (deadline_id_t){ DEADLINE_ALARM, application.id, number }) - wall_ms;
}

static void ring(void)
{
    deadline_id_t due = { 0 };
    CHECK(deadline_table_take_due(&deadlines, wall_ms, &due));
    CHECK(due.kind == DEADLINE_ALARM && due.owner == application.id && due.number == 0);
    app_host_alarm_rang(due.owner, due.number);
}

static void test_recurring_phase(void)
{
    const int64_t corrections[] = { 30000, -30000, 86400000, -86400000 };
    for (size_t index = 0; index < sizeof corrections / sizeof corrections[0]; index++) {
        deadline_table_init(&deadlines);
        memset(s_recurring, 0, sizeof s_recurring);
        wall_ms = HOST_CLOCK_MIN * MS_PER_SECOND;
        boot_ms = 1000;
        CHECK(alarm_set_every("test", 0, 60, 15));
        CHECK(alarm_set("test", 1, wall_ms / MS_PER_SECOND + 90, true));
        pass(20000); /* first occurrence is five seconds late */
        move_clock(corrections[index]);
        CHECK(alarm_left(0) == -5000);
        CHECK(alarm_left(1) == 70000 - corrections[index]);
        CHECK(alarm_set("test", 1, 0, false));
        ring();
        CHECK(alarm_left(0) == 55000); /* keep the original phase through the correction */
        pass(55000);
        ring();
        CHECK(alarm_left(0) == 60000);
        pass(180000); /* do not replay missed periods */
        ring();
        CHECK(alarm_left(0) == 60000);
        CHECK(alarm_set("test", 0, 0, false));
        CHECK(recurring_of(application.id, 0) == NULL);
    }
}

int main(void)
{
    test_recurring_phase();
    REPORT();
}
