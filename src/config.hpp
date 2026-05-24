#pragma once

struct Config {
    char wifi_ssid[64];
    char wifi_password[64];
    char mqtt_broker[64];
    int  mqtt_port;
    char mqtt_user[32];
    char mqtt_password[32];
    char ha_device_name[32];

    static Config load();
    void save() const;
};
