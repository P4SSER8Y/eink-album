#pragma once
#include <Arduino.h>

#define LOG(fmt, ...) Serial.printf(("[%0.3f][" __FILE__ ":%d] " fmt "\n"), millis() / 1000.0, __LINE__, ##__VA_ARGS__)
