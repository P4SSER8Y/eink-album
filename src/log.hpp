#pragma once

#define LOG(fmt, ...) Serial.printf(("[%0.3f] " fmt "\n"), millis() / 1000.0, ##__VA_ARGS__)
