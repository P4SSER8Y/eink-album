#include "../key.hpp"
#include "epd_7in3e.hpp"
#include "img.hpp"
#include "log.hpp"
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <cstdint>
#include <nvs_flash.h>
#include <FastLED.h>

const uint8_t PIN_SCK = 5;
const uint8_t PIN_MOSI = 4;
const uint8_t PIN_CS = 6;
const uint8_t PIN_DC = 7;
const uint8_t PIN_RST = 15;
const uint8_t PIN_BUSY = 16;
const uint8_t PIN_PWR = 3;
const uint8_t PIN_LED = 9;
const uint8_t PIN_DUMMY = 12;
const uint8_t PIN_BOOT = 0;
const auto GPIO_NUM_BOOT = GPIO_NUM_9;

EPD_7IN3E epd;

CRGB leds[1];

bool fetch_image()
{
    int len;
    int total_len;
    int idx;
    char url[256];
    snprintf(url, sizeof(url), KEY_URL);
    LOG("Start fetch an image from %s", url);
    HTTPClient client;
    client.setTimeout(60000);
    client.setConnectTimeout(5000);
    client.begin(url);
    auto code = client.GET();
    len = client.getSize();
    idx = 0;
    LOG("Get HTTP code: %d size: %d", code, len);
    if (code == HTTP_CODE_OK)
    {
        uint8_t buffer[128];
        auto stream = client.getStreamPtr();
        total_len = len;
        auto ts = millis();
        // LOG("Start read %d bytes", len);
        while (client.connected() && (len > 0 || len == -1) && (millis() - ts < 60e3))
        {
            size_t size = stream->available();
            if (size)
            {
                auto c = stream->readBytes(buffer, sizeof(buffer));
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
        LOG("End read, total %d bytes", idx);
    }
    client.end();
    return (idx == total_len);
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
    FastLED.addLeds<NEOPIXEL, 48>(leds, 1);
    FastLED.setBrightness(5);

    leds[0] = CRGB::Red;
    FastLED.show();
    LOG("Hello World");
    
    start_wifi();
    leds[0] = CRGB::Blue;
    FastLED.show();

    pinMode(PIN_BOOT, INPUT);

    epd.begin(PIN_SCK, PIN_MOSI, PIN_CS, PIN_DC, PIN_RST, PIN_BUSY, PIN_PWR, PIN_DUMMY);
    digitalWrite(PIN_LED, HIGH);
    start_wifi();
    auto flag = fetch_image();
    stop_wifi();
    leds[0] = CRGB::Yellow;
    FastLED.show();
    if (flag)
    {
        epd.flush_buffer();
        // epd.clear(BLUE);
    }
    leds[0] = CRGB::Green;
    FastLED.show();
    
    delay(60000);
    // epd.write_debug_bars();
    epd.clear(WHITE);
    leds[0] = CRGB::Black;
    FastLED.show();
}

inline bool pressed()
{
    return digitalRead(PIN_BOOT) == LOW;
}

void loop()
{
    delay(1000);
    return;
    // go_to_bed();
    if (pressed())
    {
        leds[0] = CRGB::VioletRed;
        FastLED.show();
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
            auto flag = fetch_image();
            stop_wifi();
            if (flag)
            {
                epd.flush_buffer();
                // epd.clear(BLUE);
            }
        }
        FastLED.clear();
    }
}
