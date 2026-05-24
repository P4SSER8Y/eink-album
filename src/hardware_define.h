#pragma once

#include <cstdint>

#ifdef CONFIG_PROTOTYPE_C3_SUPER_MINI
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
#endif

#ifdef CONFIG_PROTOTYPE_S3
#define LED_WS2812_CNT 1
const uint8_t PIN_SCK = 5;
const uint8_t PIN_MOSI = 4;
const uint8_t PIN_CS = 6;
const uint8_t PIN_DC = 7;
const uint8_t PIN_RST = 15;
const uint8_t PIN_BUSY = 16;
const uint8_t PIN_PWR = 3;
const uint8_t PIN_LED = 48;
const uint8_t PIN_DUMMY = 12;
const uint8_t PIN_BOOT = 0;
#endif
