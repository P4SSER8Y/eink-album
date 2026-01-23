#include "../key.hpp"
#include "epd_7in3e.hpp"
#include "img.hpp"
#include "log.hpp"
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <cstdint>
#include <nvs_flash.h>

const uint8_t PIN_SCK = 4;
const uint8_t PIN_MOSI = 6;
const uint8_t PIN_CS = 7;
const uint8_t PIN_DC = 2;
const uint8_t PIN_RST = 1;
const uint8_t PIN_BUSY = 0;
const uint8_t PIN_PWR = 3;
const uint8_t PIN_LED = 8;
const uint8_t PIN_BOOT = 9;
const auto GPIO_NUM_BOOT = GPIO_NUM_9;

EPD_7IN3E epd;

bool fetch_image(size_t since, size_t size)
{
    int len;
    int total_len;
    int idx;
    char url[256];
    snprintf(url, sizeof(url), KEY_URL, since, size);
    LOG("Start fetch an image from %s", url);
    HTTPClient client;
    client.setTimeout(30000);
    client.setConnectTimeout(5000);
    client.begin(url);
    auto code = client.GET();
    len = client.getSize();
    // LOG("Get HTTP code: %d size: %d", code, len);
    if (code == HTTP_CODE_OK)
    {
        uint8_t buffer[512];
        auto stream = client.getStreamPtr();
        total_len = len;
        idx = since;
        auto ts = millis();
        // LOG("Start read %d bytes", len);
        while (client.connected() && (len > 0 || len == -1) && (millis() - ts < 60e3))
        {
            size_t size = stream->available();
            if (size)
            {
                auto c = stream->readBytes(buffer, ((size > sizeof(buffer)) ? sizeof(buffer) : size));
                if (len > 0)
                {
                    len -= c;
                }
                for (auto i = 0; i < c; i++)
                {
                    epd.set_pixel(idx++, buffer[i]);
                }
            }
            delay(1);
        }
        // LOG("End read, total %d bytes", idx);
    }
    client.end();
    return (idx > 0) && (idx == since + size);
}

void start_wifi(void)
{
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    WiFi.mode(WIFI_STA);
    WiFi.begin(KEY_WIFI_SSID, KEY_WIFI_PASSWORD);

    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        LOG("Waiting..");
    }
    LOG("Connected to %s", KEY_WIFI_SSID);
    LOG("IP address: %s", WiFi.localIP().toString().c_str());
}

void stop_wifi()
{
    LOG("Disconnect");
    WiFi.disconnect(true);
}

void go_to_bed()
{
    digitalWrite(PIN_LED, HIGH);
    gpio_wakeup_enable(GPIO_NUM_BOOT, GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();
    esp_sleep_enable_timer_wakeup(1e6);
    esp_light_sleep_start();
    pinMode(PIN_LED, OUTPUT);
    digitalWrite(PIN_LED, HIGH);
}

void setup()
{
    Serial.begin(115200);
    LOG("Hello World");
    pinMode(PIN_BOOT, INPUT);
    pinMode(PIN_LED, OUTPUT);
    epd.begin(PIN_SCK, PIN_MOSI, PIN_CS, PIN_DC, PIN_RST, PIN_BUSY, PIN_PWR, PIN_LED);
    digitalWrite(PIN_LED, HIGH);
    epd.write_debug_bars();
    // epd.clear(WHITE);
}

inline bool pressed()
{
    return digitalRead(PIN_BOOT) == LOW;
}

void loop()
{
    // go_to_bed();
    if (pressed())
    {
        digitalWrite(PIN_LED, LOW);
        LOG("Pressed");
        auto ts = millis();
        auto last = millis();
        delay(100);
        do
        {
            delay(50);
            auto now = millis();
            auto delta = now - ts;
            if (pressed())
            {
                digitalWrite(PIN_LED, (delta % 1000 < 100) ? LOW : HIGH);
            }
            else
            {
                digitalWrite(PIN_LED, (delta % 100 < 50) ? LOW : HIGH);
            }
            if (pressed())
            {
                last = now;
            }
            else if (now - last > 1000)
            {
                break;
            }
        } while (true);
        auto delta = last - ts;
        LOG("pressed for %dms", delta);
        if (delta > 3000)
        {
            LOG("Long press");
            epd.clear(WHITE);
        }
        else if (delta > 250)
        {
            LOG("Short press");
            start_wifi();
            const auto size = 1024;
            auto flag = true;
            for (auto i = 0; i < epd.WIDTH * epd.HEIGHT; i += size)
            {
                flag &= fetch_image(i, size);
            }
            stop_wifi();
            if (flag)
            {
                epd.flush_buffer();
                // epd.clear(BLUE);
            }
        }
    }
}
