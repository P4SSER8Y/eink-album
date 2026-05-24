#include "config.hpp"
#include "../key.hpp"
#include <Preferences.h>

Config Config::load()
{
    Config cfg;
    Preferences prefs;
    prefs.begin("album", true);

    strncpy(cfg.wifi_ssid,     prefs.getString("wifi_ssid", KEY_WIFI_SSID).c_str(), sizeof(cfg.wifi_ssid) - 1);
    strncpy(cfg.wifi_password, prefs.getString("wifi_pwd", KEY_WIFI_PASSWORD).c_str(), sizeof(cfg.wifi_password) - 1);
    strncpy(cfg.mqtt_broker,   prefs.getString("mqtt_host", "").c_str(), sizeof(cfg.mqtt_broker) - 1);
    cfg.mqtt_port              = prefs.getInt("mqtt_port", 1883);
    strncpy(cfg.mqtt_user,     prefs.getString("mqtt_user", "").c_str(), sizeof(cfg.mqtt_user) - 1);
    strncpy(cfg.mqtt_password, prefs.getString("mqtt_pwd", "").c_str(), sizeof(cfg.mqtt_password) - 1);
    strncpy(cfg.ha_device_name,prefs.getString("ha_name", "eink_album").c_str(), sizeof(cfg.ha_device_name) - 1);

    prefs.end();
    return cfg;
}

void Config::save() const
{
    Preferences prefs;
    prefs.begin("album", false);

    prefs.putString("wifi_ssid", wifi_ssid);
    prefs.putString("wifi_pwd", wifi_password);
    prefs.putString("mqtt_host", mqtt_broker);
    prefs.putInt("mqtt_port", mqtt_port);
    prefs.putString("mqtt_user", mqtt_user);
    prefs.putString("mqtt_pwd", mqtt_password);
    prefs.putString("ha_name", ha_device_name);

    prefs.end();
}
