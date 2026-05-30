#include "../key.hpp"
#include "config.hpp"
#include "epd_7in3e.hpp"
#include "hardware_define.h"
#include "http_server.hpp"
#include "indicator.hpp"
#include "log.hpp"
#include "mqtt_ha.hpp"
#include <Arduino.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs_flash.h>

static Config cfg;
EPD_7IN3E epd;
static TaskHandle_t epd_task_handle = nullptr;
volatile bool epd_busy = false;
volatile bool epd_flush_done = false;

static void epd_task_fn(void *);

static void connect_wifi()
{
    nvs_flash_init();
    WiFi.mode(WIFI_STA);
    WiFi.begin(cfg.wifi_ssid, cfg.wifi_password);

    Indicator->set_state(IIndicator::WIFI_Connecting);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        LOG("Waiting for WiFi...");
    }
    LOG("WiFi connected, IP: %s", WiFi.localIP().toString().c_str());
    Indicator->set_state(IIndicator::WIFI_Connected);
}

void setup()
{
    Serial.begin(115200);
    Serial.flush();
    Serial.println("\n=== E-Ink Album Starting ===");
    delay(3000);
    init_indicator();
    LOG("Indicator initialized");
    Indicator->set_state(IIndicator::Busy);

    epd.begin(PIN_SCK, PIN_MOSI, PIN_CS, PIN_DC, PIN_RST, PIN_BUSY, PIN_PWR, PIN_DUMMY);

    // color_index_t colors[] = {BLACK, WHITE, YELLOW, RED, BLUE, GREEN};
    // auto rcolor = colors[esp_random() % 6];
    // LOG("Random fill with color %d", rcolor);
    // epd.clear(rcolor);

    cfg = Config::load();

    connect_wifi();

    http_server_begin(cfg);
    mqtt.begin(cfg);

    mqtt.publish_ip(WiFi.localIP().toString().c_str());

    if (mqtt.is_connected()) {
        mqtt.publish_random();
    }

    xTaskCreatePinnedToCore(epd_task_fn, "epd", 4096, nullptr, 1, &epd_task_handle, 1);

    Indicator->set_state(IIndicator::Idle);
    mqtt.publish_status(MqttHA::Restarted);
    mqtt.publish_status(MqttHA::Idle);

    LOG("Setup complete, free heap=%d psram=%d", ESP.getFreeHeap(), ESP.getFreePsram());
}

static void ensure_wifi()
{
    if (WiFi.status() == WL_CONNECTED)
        return;

    LOG("WiFi disconnected, reconnecting...");
    Indicator->set_state(IIndicator::WIFI_Connecting);
    WiFi.disconnect();
    WiFi.mode(WIFI_STA);
    WiFi.begin(cfg.wifi_ssid, cfg.wifi_password);

    int retries = 0;
    while (WiFi.status() != WL_CONNECTED && retries < 50) {
        delay(500);
        retries++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        LOG("WiFi reconnected, IP: %s", WiFi.localIP().toString().c_str());
        Indicator->set_state(IIndicator::WIFI_Connected);
    } else {
        LOG("WiFi reconnect failed after %d retries", retries);
        Indicator->set_state(IIndicator::WIFI_Connecting);
    }
}

static void epd_task_fn(void *)
{
    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        epd.flush_buffer();
        epd_flush_done = true;
        epd_busy = false;
    }
}

void loop()
{
    static unsigned long last_wifi_check = 0;
    static unsigned long last_mqtt_attempt = 0;
    static unsigned long last_status_log = 0;

    server.handleClient();

    if (millis() - last_wifi_check > 10000) {
        last_wifi_check = millis();
        ensure_wifi();
    }

    if (mqtt.is_connected()) {
        mqtt.loop();
    }

    if (Indicator) Indicator->tick();

    if (Indicator && mqtt.indicator_cmd_pending()) {
        auto cmd = mqtt.consume_indicator_cmd();
        if (cmd.clear || (cmd.timeout_s == 0 && cmd.freq_hz == 0)) {
            Indicator->remote_effect_clear();
            mqtt.publish_indicator_state(false, 0, 0, 0);
        } else {
            Indicator->remote_effect(cmd.r, cmd.g, cmd.b, cmd.freq_hz, cmd.timeout_s);
            mqtt.publish_indicator_state(true, cmd.r, cmd.g, cmd.b);
        }
    }

    if (millis() - last_status_log > 10000) {
        last_status_log = millis();
        LOG("MQTT status: connected=%d, broker=%s", mqtt.is_connected(), cfg.mqtt_broker);
    }

    if (mqtt_needs_reconnect || (!mqtt.is_connected() && millis() - last_mqtt_attempt > 30000)) {
        mqtt_needs_reconnect = false;
        last_mqtt_attempt = millis();
        LOG("Attempting MQTT connect to %s:%d", cfg.mqtt_broker, cfg.mqtt_port);
        mqtt.begin(cfg);
        if (mqtt.is_connected())
            mqtt.publish_status(MqttHA::Idle);
    }

    if (image_uploaded && !epd_busy) {
        image_uploaded = false;
        epd_busy = true;
        epd_flush_done = false;

        Indicator->set_state(IIndicator::Updating);
        mqtt.publish_status(MqttHA::Updating);
        xTaskNotifyGive(epd_task_handle);
    }

    if (epd_flush_done) {
        epd_flush_done = false;
        mqtt.publish_status(MqttHA::Done);
        Indicator->set_state(IIndicator::Idle);
        mqtt.publish_status(MqttHA::Idle);
    }

    delay(10);
}
