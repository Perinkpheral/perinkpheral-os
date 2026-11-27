/* SPDX-License-Identifier: MIT */
#include "tinytest.h"

#define CONFIG_APP_EPD_MOSI_GPIO 18
#define CONFIG_APP_EPD_SCLK_GPIO 19
#define CONFIG_APP_EPD_CS_GPIO 17
#define CONFIG_APP_EPD_DC_GPIO 23
#define CONFIG_APP_EPD_RST_GPIO 22
#define CONFIG_APP_EPD_BUSY_GPIO 16
#define ESP_ERROR_CHECK(expression) CHECK((expression) == ESP_OK)
#include "../../components/epd/display.c"

static int64_t clock_ms;
static int full_shows, partial_shows;

int64_t esp_timer_get_time(void) { return clock_ms * 1000; }
SemaphoreHandle_t xSemaphoreCreateMutex(void) { return &clock_ms; }
int xSemaphoreTake(SemaphoreHandle_t semaphore, unsigned wait)
{
    (void)semaphore; (void)wait; return 1;
}
void xSemaphoreGive(SemaphoreHandle_t semaphore) { (void)semaphore; }
bool deadlines_has(deadline_kind_t kind, uint32_t owner, int number)
{
    (void)kind; (void)owner; (void)number; return false;
}
bool deadlines_set_in(deadline_kind_t kind, uint32_t owner, int number, int64_t in_ms)
{
    (void)kind; (void)owner; (void)number; (void)in_ms; return true;
}
void deadlines_cancel(deadline_kind_t kind, uint32_t owner, int number)
{
    (void)kind; (void)owner; (void)number;
}
esp_err_t epd_init(const epd_config_t *config) { (void)config; return ESP_OK; }
esp_err_t epd_show_full(const uint8_t *frame, bool fast)
{
    (void)frame; (void)fast; full_shows++; return ESP_OK;
}
esp_err_t epd_show_partial(const uint8_t *frame, const frame_box_t *window, bool awake)
{
    (void)frame; (void)window; (void)awake; partial_shows++; return ESP_OK;
}
void epd_sleep(void) {}

int main(void)
{
    const refresh_policy_cfg_t policy = { .min_data_ms = 60000, .keepalive_ms = 86400000 };
    static uint8_t frame[DISPLAY_FRAME_SIZE];
    memset(frame, 0xFF, sizeof frame);
    display_start(&policy);
    CHECK(display_show(frame, REFRESH_FOR_SWITCH) == DISPLAY_SHOWN);
    CHECK(full_shows == 1);
    CHECK(display_show(frame, REFRESH_FOR_DATA) == DISPLAY_UNCHANGED);
    CHECK(display_show(frame, REFRESH_FOR_CLEAN) == DISPLAY_DEFERRED);
    CHECK(full_shows == 1);
    clock_ms = 1000;
    CHECK(display_show(frame, REFRESH_FOR_CLEAN) == DISPLAY_SHOWN);
    CHECK(full_shows == 2 && partial_shows == 0);
    CHECK(display_show(frame, REFRESH_FOR_CLEAN) == DISPLAY_DEFERRED);
    clock_ms = 2000;
    CHECK(display_show(frame, REFRESH_FOR_CLEAN) == DISPLAY_SHOWN);
    CHECK(full_shows == 3);
    CHECK(display_show(frame, REFRESH_FOR_BUTTON) == DISPLAY_UNCHANGED);
    REPORT();
}
