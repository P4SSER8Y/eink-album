#include "../key.hpp"
#include "epd_7in3e.hpp"
#include "img.hpp"
#include "log.hpp"
#include <Arduino.h>
#include <cstdint>

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

EPD_7IN3E epd{PIN_SCK, PIN_MOSI, PIN_CS, PIN_DC, PIN_RST, PIN_BUSY, PIN_PWR, PIN_LED};

inline void load_default_image()
{
    for (auto i = 0; i < IMG_SIZE; i++)
    {
        epd.set_pixel(i, DEFAULT_IMG[i]);
    }
}

void go_to_bed()
{
    LOG("Time for bed...");
    digitalWrite(PIN_LED, HIGH);
    gpio_wakeup_enable(GPIO_NUM_BOOT, GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();
    esp_sleep_enable_timer_wakeup(1e6);
    esp_light_sleep_start();
    LOG("Good Morning!");
    Serial.begin(115200);
    pinMode(PIN_LED, OUTPUT);
    digitalWrite(PIN_LED, HIGH);
}

void setup()
{
    load_default_image();
    Serial.begin(115200);
    pinMode(PIN_BOOT, INPUT);
    pinMode(PIN_LED, OUTPUT);
}

inline bool pressed()
{
    return digitalRead(PIN_BOOT) == LOW;
}

void loop()
{
    go_to_bed();
    if (pressed())
    {
        digitalWrite(PIN_LED, LOW);
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
            epd.flush_buffer();
        }
    }
}
