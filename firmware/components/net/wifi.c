/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "priorities.h"
#include "board.h"
#include "wifi.h"
#include "wifi_status.h"
#include "wifi_credentials.h"
#include "wifi_events.h"
#include "wifi_search.h"

#include <string.h>
#include <stdatomic.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "captive_dns.h"
#include "captive_portal.h"
#include "deadlines.h"
#include "units.h"

static const char *TAG = "wifi";

#define BSSID_BYTES 6 /* the access point's address on the radio */
#define PASSWORD_MAX            64
#define ACCESS_POINT_CHANNEL    1
#define ACCESS_POINT_CLIENTS    4
#define TASK_STACK_BYTES        STACK_WIFI_PLAN
#define SCAN_WAIT_MS            8000
#define SCAN_CHANNEL_MS         120
#define SCAN_HOME_MS            30
#define SCAN_DONE_BIT           BIT0

static struct {
    char ssid[WIFI_SSID_MAX + 1];
    char password[PASSWORD_MAX + 1];
    char access_point_name[WIFI_SSID_MAX + 1];
    void (*on_change)(void);

    wifi_plan_t        plan; /* the Wi-Fi task's alone */
    TaskHandle_t       task;
    atomic_uint        pending_events;
    bool               access_point_up;
    bool               radio_off; /* the Wi-Fi task's alone */
    bool               is_starting_again; /* the Wi-Fi task's alone: wanted on, would not start */
    /* radio_off as the ESP event task may read it: the disconnect that
     * stopping the radio brings is ours, not a lost network. */
    atomic_bool        radio_stopped;
    /* Scan holds this while driver work is in progress, delaying retries and
     * power changes without blocking the ESP event task that completes scans. */
    SemaphoreHandle_t driver_lock;
    bool               connecting;
    EventGroupHandle_t scan_events;
    atomic_bool        scan_pending;
    atomic_uint        scan_status;

    /* What other tasks may ask about, under the lock. */
    SemaphoreHandle_t lock;
    wifi_status_t     status;
} s_wifi;

static int64_t now_ms(void)
{
    return esp_timer_get_time() / US_PER_MS;
}

/* State stays pending while a scan holds the driver lock. */
static void post(wifi_plan_event_t event)
{
    wifi_events_post(&s_wifi.pending_events, event);
    xTaskNotifyGive(s_wifi.task);
}

/* A radio that would not start - out of memory, say - is tried again this
 * long after, while it is wanted. */
#define RADIO_START_AGAIN_MS 5000

/* Where the network was found last: the access point and its channel.
 * With it the next connection looks in one place instead of scanning
 * every channel - a second or two less of each time the radio comes up
 * for a poll, which on a battery is most of what a poll costs. */
static struct {
    bool    is_known;
    uint8_t access_point[BSSID_BYTES];
    uint8_t channel;
    char    ssid[WIFI_SSID_MAX + 1];
} s_found_at;

/* The attempt under way looks only where the network was last: if it
 * fails, that place is forgotten and the next one looks everywhere. */
static bool s_is_looking_in_one_place;

/* Access points that would not let the device in since it was last on the network, and
 * whether the attempt under way got as far as joining its point. */
static wifi_avoided_t s_avoided;
static atomic_bool s_joined_point;

static void on_station_joined(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    (void)id;
    (void)data;
    atomic_store(&s_joined_point, true);
}

#define SEARCH_POINTS_MAX 8

/* The access points carrying the network's name, by a scan in the plan's task, which holds
 * the driver: the page's scan answers busy meanwhile. */
static int scan_points(wifi_point_t *points, int capacity)
{
    wifi_scan_config_t config = {
        .ssid = (uint8_t *)s_wifi.ssid,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active = { .min = 0, .max = SCAN_CHANNEL_MS },
        .home_chan_dwell_time = SCAN_HOME_MS,
    };
    esp_err_t error = esp_wifi_scan_start(&config, true);
    if (error != ESP_OK) {
        ESP_LOGW(TAG, "cannot scan for the network's points: %s", esp_err_to_name(error));
        return 0;
    }
    uint16_t found = 0;
    int count = 0;
    if (esp_wifi_scan_get_ap_num(&found) == ESP_OK) {
        for (uint16_t index = 0; index < found; index++) {
            wifi_ap_record_t record;
            if (esp_wifi_scan_get_ap_record(&record) != ESP_OK) {
                break;
            }
            if (count < capacity) {
                memcpy(points[count].bssid, record.bssid, sizeof points[count].bssid);
                points[count].channel = record.primary;
                points[count].rssi = record.rssi;
                points[count].secured = record.authmode != WIFI_AUTH_OPEN;
                count++;
            }
        }
    }
    esp_wifi_clear_ap_list();
    return count;
}

static void remember_where_found(void)
{
    wifi_ap_record_t found;
    if (esp_wifi_sta_get_ap_info(&found) != ESP_OK) {
        return;
    }
    ESP_LOGI(TAG, "joined \"%s\" at %02x:%02x:%02x:%02x:%02x:%02x, channel %d, %d dBm%s", s_wifi.ssid,
             found.bssid[0], found.bssid[1], found.bssid[2], found.bssid[3], found.bssid[4],
             found.bssid[5], found.primary, found.rssi, s_is_looking_in_one_place ? ", as last time" : "");
    memcpy(s_found_at.access_point, found.bssid, sizeof s_found_at.access_point);
    s_found_at.channel = found.primary;
    strlcpy(s_found_at.ssid, s_wifi.ssid, sizeof s_found_at.ssid);
    s_found_at.is_known = true;
}

static void on_got_address(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    (void)id;
    const ip_event_got_ip_t *got = data;
    xSemaphoreTake(s_wifi.lock, portMAX_DELAY);
    esp_ip4addr_ntoa(&got->ip_info.ip, s_wifi.status.address, sizeof s_wifi.status.address);
    xSemaphoreGive(s_wifi.lock);
    remember_where_found();
    wifi_avoided_clear(&s_avoided);
    s_is_looking_in_one_place = false;
    post(WIFI_EVENT_CONNECTED);
}

static void on_station_disconnected(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    (void)id;
    const wifi_event_sta_disconnected_t *lost = data;
    if (atomic_load(&s_wifi.radio_stopped)) {
        ESP_LOGI(TAG, "radio off to save power");
    } else {
        /* The access point and the signal it was last heard at: which of a
         * mesh's points dropped the device, and how far away it was. */
        ESP_LOGW(TAG, "not connected to \"%s\", reason %d, %02x:%02x:%02x:%02x:%02x:%02x, %d dBm%s",
                 s_wifi.ssid, lost->reason, lost->bssid[0], lost->bssid[1], lost->bssid[2],
                 lost->bssid[3], lost->bssid[4], lost->bssid[5], lost->rssi,
                 s_is_looking_in_one_place ? ", as last time" : "");
    }
    if (!atomic_load(&s_wifi.radio_stopped)) {
        xSemaphoreTake(s_wifi.lock, portMAX_DELAY);
        wifi_status_lost(&s_wifi.status);
        xSemaphoreGive(s_wifi.lock);
    }
    if (!atomic_load(&s_wifi.radio_stopped) &&
        wifi_search_point_refused(atomic_exchange(&s_joined_point, false), lost->reason)) {
        /* This point would not let the device in: the next attempt tries another. */
        wifi_avoided_add(&s_avoided, lost->bssid);
    }
    if (s_is_looking_in_one_place) {
        /* Not there any more - another channel, another access point. */
        s_is_looking_in_one_place = false;
        s_found_at.is_known = false;
    }
    post(WIFI_EVENT_DISCONNECTED);
}

/* The radio is running: only now can it be told to connect. */
static void on_station_started(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    (void)id;
    (void)data;
    ESP_ERROR_CHECK(esp_wifi_set_max_tx_power(BOARD_WIFI_TX_POWER_DBM * 4));
    int8_t power;
    ESP_ERROR_CHECK(esp_wifi_get_max_tx_power(&power));
    ESP_LOGI(TAG, "TX power limit: %d quarter-dBm", power);
    post(WIFI_EVENT_STARTED);
}

static void on_scan_done(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    (void)id;
    if (!atomic_load(&s_wifi.scan_pending)) {
        return;
    }
    const wifi_event_sta_scan_done_t *done = data;
    atomic_store(&s_wifi.scan_status, done->status);
    xEventGroupSetBits(s_wifi.scan_events, SCAN_DONE_BIT);
    /* Keep a timed-out scan claimed until its final event has been consumed:
     * a late completion must never wake the next caller. */
    atomic_store(&s_wifi.scan_pending, false);
}

void wifi_retry_is_due(void)
{
    if (s_wifi.task != NULL) {
        post(WIFI_EVENT_RETRY_DUE);
    }
}

void wifi_rescue_is_due(void)
{
    if (s_wifi.task != NULL) {
        post(WIFI_EVENT_RESCUE_DUE);
    }
}

static bool start_access_point(void)
{
    wifi_config_t config = { 0 };
    config.ap.ssid_len = (uint8_t)wifi_copy_field(config.ap.ssid, sizeof config.ap.ssid,
                                                s_wifi.access_point_name);
    config.ap.channel = ACCESS_POINT_CHANNEL;
    config.ap.authmode = WIFI_AUTH_OPEN;
    config.ap.max_connection = ACCESS_POINT_CLIENTS;
    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (err == ESP_OK) {
        err = esp_wifi_set_config(WIFI_IF_AP, &config);
    }
    if (err != ESP_OK) {
        /* Without it the device cannot be set up, but it must not go
         * into a restart loop over that either. */
        ESP_LOGE(TAG, "cannot bring the setup access point up: %s", esp_err_to_name(err));
        return false;
    }
    s_wifi.access_point_up = true;
    xSemaphoreTake(s_wifi.lock, portMAX_DELAY);
    s_wifi.status.access_point_up = true;
    xSemaphoreGive(s_wifi.lock);
    captive_dns_enable(true);
    ESP_LOGI(TAG, "setup access point \"%s\" is up", s_wifi.access_point_name);
    return true;
}

static void stop_access_point(void)
{
    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "cannot take the setup access point down: %s", esp_err_to_name(err));
        return;
    }
    s_wifi.access_point_up = false;
    xSemaphoreTake(s_wifi.lock, portMAX_DELAY);
    s_wifi.status.access_point_up = false;
    xSemaphoreGive(s_wifi.lock);
    captive_dns_enable(false);
    ESP_LOGI(TAG, "setup access point is down");
}

static void connect(void)
{
    wifi_config_t config = { 0 };
    wifi_copy_field(config.sta.ssid, sizeof config.sta.ssid, s_wifi.ssid);
    wifi_copy_field(config.sta.password, sizeof config.sta.password, s_wifi.password);
    config.sta.threshold.authmode =
        s_wifi.password[0] != '\0' ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    s_is_looking_in_one_place =
        s_found_at.is_known && strcmp(s_found_at.ssid, s_wifi.ssid) == 0;
    if (s_is_looking_in_one_place) {
        config.sta.bssid_set = true;
        memcpy(config.sta.bssid, s_found_at.access_point, sizeof config.sta.bssid);
        config.sta.channel = s_found_at.channel;
        config.sta.scan_method = WIFI_FAST_SCAN;
    } else {
        /* Every access point of the name, the strongest first - unless one would not let the
         * device in: then the strongest of the others, found by a scan of their own. */
        config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
        config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
        if (s_avoided.count > 0) {
            wifi_point_t points[SEARCH_POINTS_MAX];
            int found = scan_points(points, SEARCH_POINTS_MAX);
            int pick = wifi_search_pick(points, found, &s_avoided, s_wifi.password[0] != '\0');
            if (pick >= 0) {
                config.sta.bssid_set = true;
                memcpy(config.sta.bssid, points[pick].bssid, sizeof config.sta.bssid);
                config.sta.channel = points[pick].channel;
                config.sta.scan_method = WIFI_FAST_SCAN;
                ESP_LOGI(TAG, "trying %02x:%02x:%02x:%02x:%02x:%02x, channel %d, %d dBm: %d of %d points would not take the device",
                         points[pick].bssid[0], points[pick].bssid[1], points[pick].bssid[2],
                         points[pick].bssid[3], points[pick].bssid[4], points[pick].bssid[5],
                         points[pick].channel, points[pick].rssi, s_avoided.count, found);
            } else if (found > 0) {
                /* Every point found has refused once: they may take it now. */
                ESP_LOGW(TAG, "every point of \"%s\" refused the device; trying them all again", s_wifi.ssid);
                wifi_avoided_clear(&s_avoided);
            }
        }
    }
    /* Before the attempt starts: its joining may be told before esp_wifi_connect() returns. */
    atomic_store(&s_joined_point, false);
    esp_err_t err = esp_wifi_set_mode(s_wifi.access_point_up ? WIFI_MODE_APSTA : WIFI_MODE_STA);
    if (err == ESP_OK) {
        err = esp_wifi_set_config(WIFI_IF_STA, &config);
    }
    if (err == ESP_OK) {
        err = esp_wifi_connect();
    }
    s_wifi.connecting = err == ESP_OK;
    if (err != ESP_OK) {
        /* The plan hears of it as of any other failed attempt. */
        ESP_LOGW(TAG, "cannot start connecting: %s", esp_err_to_name(err));
        /* If it was the place that the radio would not take, not again. */
        s_is_looking_in_one_place = false;
        s_found_at.is_known = false;
        post(WIFI_EVENT_DISCONNECTED);
    }
}

static void carry_out(const wifi_plan_actions_t *actions)
{
    if (actions->start_access_point) {
        uint32_t retry_ms = wifi_plan_access_point_result(&s_wifi.plan, start_access_point());
        if (retry_ms > 0) {
            deadlines_set_in(DEADLINE_WIFI_RESCUE, 0, 0, retry_ms);
        } else {
            deadlines_cancel(DEADLINE_WIFI_RESCUE, 0, 0);
        }
    }
    if (actions->stop_access_point) {
        stop_access_point();
    }
    if (actions->connect) {
        connect();
    }
    if (actions->rescue_in_ms > 0) {
        deadlines_set_in(DEADLINE_WIFI_RESCUE, 0, 0, actions->rescue_in_ms);
    }
    if (actions->retry_in_ms > 0) {
        deadlines_set_in(DEADLINE_WIFI_RETRY, 0, 0, actions->retry_in_ms);
    }
}

static void publish_phase(void)
{
    xSemaphoreTake(s_wifi.lock, portMAX_DELAY);
    bool changed = wifi_status_update(&s_wifi.status, s_wifi.plan.phase,
                                      s_wifi.radio_off);
    xSemaphoreGive(s_wifi.lock);
    if (changed && s_wifi.on_change != NULL) {
        s_wifi.on_change();
    }
}

/* The radio stopped to save power is wanted again. */
static void start_radio(void)
{
    s_wifi.radio_off = false;
    atomic_store(&s_wifi.radio_stopped, false);
    /* A new run of attempts: the old failures are not counted. */
    wifi_plan_init(&s_wifi.plan, true, false);
    esp_err_t err = esp_wifi_start(); /* and the radio says when it has started */
    s_wifi.is_starting_again = err != ESP_OK;
    if (err != ESP_OK) {
        /* Still stopped, and tried again: taken as on, it would wait for a start
         * that never comes, and no later request could turn it either way. */
        ESP_LOGW(TAG, "cannot start the radio: %s", esp_err_to_name(err));
        s_wifi.radio_off = true;
        atomic_store(&s_wifi.radio_stopped, true);
        deadlines_set_in(DEADLINE_WIFI_RETRY, 0, 0, RADIO_START_AGAIN_MS);
    }
}

static void handle_event(wifi_plan_event_t event)
{
    if (event == RADIO_OFF_ASKED) {
        if (s_wifi.is_starting_again) {
            /* Not wanted any more: no more tries. */
            s_wifi.is_starting_again = false;
            deadlines_cancel(DEADLINE_WIFI_RETRY, 0, 0);
        }
        /* Only a device that is where it should be may go dark. */
        if (!s_wifi.radio_off && s_wifi.plan.has_network &&
            !wifi_phase_has_access_point(s_wifi.plan.phase)) {
            if (s_wifi.plan.phase != WIFI_PHASE_ONLINE) {
                xSemaphoreTake(s_wifi.lock, portMAX_DELAY);
                wifi_status_lost(&s_wifi.status);
                xSemaphoreGive(s_wifi.lock);
            }
            s_wifi.radio_off = true;
            atomic_store(&s_wifi.radio_stopped, true);
            s_wifi.connecting = false;
            deadlines_cancel(DEADLINE_WIFI_RETRY, 0, 0);
            deadlines_cancel(DEADLINE_WIFI_RESCUE, 0, 0);
            esp_wifi_stop();
        }
        publish_phase();
        return;
    }
    if (event == RADIO_ON_ASKED) {
        if (s_wifi.radio_off) {
            start_radio();
        }
        publish_phase();
        return;
    }
    if (s_wifi.radio_off) {
        if (event == WIFI_EVENT_RETRY_DUE && s_wifi.is_starting_again) {
            start_radio();
            publish_phase();
        }
        return; /* else the last words of a radio that was turned off */
    }
    if (event == WIFI_EVENT_CONNECTED || event == WIFI_EVENT_DISCONNECTED) {
        s_wifi.connecting = false;
    }
    if (event == WIFI_EVENT_CONNECTED) {
        deadlines_cancel(DEADLINE_WIFI_RESCUE, 0, 0);
    }
    wifi_plan_actions_t actions = wifi_plan_step(&s_wifi.plan, event, now_ms());
    carry_out(&actions);
    publish_phase();
}

static void wifi_task(void *arg)
{
    (void)arg;
    for (;;) {
        /* The first event is the radio saying it has started. */
        wifi_plan_event_t event;
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        xSemaphoreTake(s_wifi.driver_lock, portMAX_DELAY);
        while (wifi_events_take(&s_wifi.pending_events, &event)) {
            handle_event(event);
        }
        xSemaphoreGive(s_wifi.driver_lock);
    }
}

/* The lease names the portal (DHCP option 114, RFC 8910), so a phone
 * that reads it need not find the portal by probing. The server keeps
 * the pointer, and takes an option only while stopped. Without it the
 * name server and the redirect still lead there. */
static void advertise_portal(esp_netif_t *access_point)
{
    static char portal[] = CAPTIVE_PORTAL_URL;
    esp_err_t stopped = esp_netif_dhcps_stop(access_point);
    if (stopped != ESP_OK && stopped != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) {
        ESP_LOGW(TAG, "portal not advertised: %s", esp_err_to_name(stopped));
        return;
    }
    esp_err_t set = esp_netif_dhcps_option(access_point, ESP_NETIF_OP_SET,
                                           ESP_NETIF_CAPTIVEPORTAL_URI, portal,
                                           sizeof portal - 1);
    if (set != ESP_OK) {
        ESP_LOGW(TAG, "portal not advertised: %s", esp_err_to_name(set));
    }
    if (stopped == ESP_OK) {
        esp_netif_dhcps_start(access_point);
    }
}

void wifi_start(const wifi_config_in_t *config)
{
    strlcpy(s_wifi.ssid, config->ssid, sizeof s_wifi.ssid);
    strlcpy(s_wifi.password, config->password, sizeof s_wifi.password);
    strlcpy(s_wifi.access_point_name, config->access_point_name,
            sizeof s_wifi.access_point_name);
    s_wifi.on_change = config->on_change;
    wifi_plan_init(&s_wifi.plan, s_wifi.ssid[0] != '\0', config->is_off);
    s_wifi.status.phase = s_wifi.plan.phase;

    s_wifi.lock = xSemaphoreCreateMutex();
    configASSERT(s_wifi.lock != NULL);
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    if (!wifi_phase_has_radio(s_wifi.plan.phase)) {
        /* No task and no driver lock: what is asked of the radio from
         * here on finds neither and does nothing. */
        ESP_LOGI(TAG, "Wi-Fi is off");
        return;
    }
    s_wifi.driver_lock = xSemaphoreCreateMutex();
    s_wifi.scan_events = xEventGroupCreate();
    configASSERT(s_wifi.driver_lock != NULL &&
                 s_wifi.scan_events != NULL);

    advertise_portal(esp_netif_create_default_wifi_ap());
    /* A phone that joins the access point is told that every name is the
     * device, so it opens the device's page without being asked. */
    static const uint8_t SETUP_ADDRESS[4] = { 192, 168, 4, 1 };
    captive_dns_start(SETUP_ADDRESS);
    esp_netif_t *station = esp_netif_create_default_wifi_sta();
    /* A name the network stack will not take is not worth refusing to
     * start over: the device then goes by its default name. */
    esp_err_t hostname_result = esp_netif_set_hostname(station, config->hostname);
    if (hostname_result != ESP_OK) {
        ESP_LOGW(TAG, "hostname \"%s\" not taken: %s", config->hostname,
                 esp_err_to_name(hostname_result));
    }

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, WIFI_EVENT_STA_START,
                                                        on_station_started, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, on_station_disconnected, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, WIFI_EVENT_SCAN_DONE, on_scan_done, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, WIFI_EVENT_STA_CONNECTED, on_station_joined, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                        on_got_address, NULL, NULL));
    /* The settings are the host's to keep, not the Wi-Fi library's. */
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    /* The task first, so that it is there to take the start event. */
    BaseType_t ok = xTaskCreate(wifi_task, "wifi", TASK_STACK_BYTES, NULL, PRIORITY_SERVER, &s_wifi.task);
    configASSERT(ok == pdPASS);
    ESP_ERROR_CHECK(esp_wifi_start());
}

void wifi_get_status(wifi_status_t *status)
{
    memset(status, 0, sizeof *status);
    strlcpy(status->ssid, s_wifi.ssid, sizeof status->ssid);
    strlcpy(status->access_point_name, s_wifi.access_point_name,
            sizeof status->access_point_name);
    if (s_wifi.lock == NULL) {
        return; /* not started: everything stays empty, the phase is setup */
    }
    xSemaphoreTake(s_wifi.lock, portMAX_DELAY);
    status->phase = s_wifi.status.phase;
    status->is_radio_off = s_wifi.status.is_radio_off;
    status->connection_known = s_wifi.status.connection_known;
    status->connection_generation = s_wifi.status.connection_generation;
    status->was_reached = s_wifi.status.was_reached;
    status->access_point_up = s_wifi.status.access_point_up;
    strlcpy(status->address, s_wifi.status.address, sizeof status->address);
    xSemaphoreGive(s_wifi.lock);
}

bool wifi_signal(int *rssi)
{
    if (!wifi_is_online()) {
        return false;
    }
    return esp_wifi_sta_get_rssi(rssi) == ESP_OK;
}

void wifi_set_radio(bool on)
{
    if (s_wifi.task != NULL) {
        post(on ? RADIO_ON_ASKED : RADIO_OFF_ASKED);
    }
}

bool wifi_is_online(void)
{
    wifi_status_t status;
    wifi_get_status(&status);
    return status.phase == WIFI_PHASE_ONLINE;
}

wifi_scan_result_t wifi_scan_networks(wifi_network_t *records, uint16_t capacity,
                                      uint16_t *count)
{
    if (count != NULL) {
        *count = 0;
    }
    if (records == NULL || count == NULL || capacity == 0 || s_wifi.driver_lock == NULL) {
        return WIFI_SCAN_FAILED;
    }
    if (capacity > WIFI_SCAN_NETWORKS_MAX) {
        capacity = WIFI_SCAN_NETWORKS_MAX;
    }
    if (xSemaphoreTake(s_wifi.driver_lock, 0) != pdTRUE) {
        return WIFI_SCAN_BUSY;
    }
    if (s_wifi.radio_off || s_wifi.connecting || s_wifi.plan.phase == WIFI_PHASE_CONNECTING ||
        atomic_load(&s_wifi.scan_pending)) {
        xSemaphoreGive(s_wifi.driver_lock);
        return WIFI_SCAN_BUSY;
    }
    xEventGroupClearBits(s_wifi.scan_events, SCAN_DONE_BIT);
    atomic_store(&s_wifi.scan_pending, true);
    wifi_scan_config_t config = {
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active = { .min = 0, .max = SCAN_CHANNEL_MS },
        .home_chan_dwell_time = SCAN_HOME_MS,
    };
    esp_err_t error = esp_wifi_scan_start(&config, false);
    wifi_scan_result_t result = WIFI_SCAN_FAILED;
    if (error != ESP_OK) {
        atomic_store(&s_wifi.scan_pending, false);
        result = error == ESP_ERR_WIFI_STATE ? WIFI_SCAN_BUSY : WIFI_SCAN_FAILED;
        ESP_LOGW(TAG, "cannot start network scan: %s", esp_err_to_name(error));
        goto finished;
    }
    EventBits_t completed = xEventGroupWaitBits(s_wifi.scan_events, SCAN_DONE_BIT,
                                               pdTRUE, pdFALSE, pdMS_TO_TICKS(SCAN_WAIT_MS));
    if (!(completed & SCAN_DONE_BIT)) {
        ESP_LOGW(TAG, "network scan timed out");
        error = esp_wifi_scan_stop();
        if (error != ESP_OK) {
            ESP_LOGW(TAG, "cannot stop network scan: %s", esp_err_to_name(error));
        }
        goto clear_results;
    }
    if (atomic_load(&s_wifi.scan_status) != 0) {
        goto clear_results;
    }
    uint16_t found = 0;
    if (esp_wifi_scan_get_ap_num(&found) != ESP_OK) {
        goto clear_results;
    }
    for (uint16_t index = 0; index < found; index++) {
        wifi_ap_record_t access_point;
        if (esp_wifi_scan_get_ap_record(&access_point) != ESP_OK) {
            *count = 0;
            goto clear_results;
        }
        wifi_network_t network = {
            .rssi = access_point.rssi,
            .secured = access_point.authmode != WIFI_AUTH_OPEN,
        };
        memcpy(network.ssid, access_point.ssid, WIFI_SSID_MAX);
        wifi_networks_add(records, capacity, count, &network);
    }
    result = WIFI_SCAN_OK;
clear_results:
    /* Single-record reads free each record; also clear on timeout and errors. */
    error = esp_wifi_clear_ap_list();
    if (error != ESP_OK) {
        ESP_LOGW(TAG, "cannot clear network scan: %s", esp_err_to_name(error));
        result = WIFI_SCAN_FAILED;
        *count = 0;
    }
finished:
    xSemaphoreGive(s_wifi.driver_lock);
    return result;
}
