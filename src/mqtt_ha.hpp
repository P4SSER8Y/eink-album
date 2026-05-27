#pragma once
#include "config.hpp"
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <WiFi.h>

class MqttHA {
public:
    enum Status { Idle, Fetching, Updating, Done, Error, Restarted };

    void begin(const Config &cfg);
    void loop();
    void publish_status(Status st);
    void publish_ip(const char *ip);
    bool is_connected() { return client.connected(); }
    bool refresh_requested() { bool v = _refresh_pending; _refresh_pending = false; return v; }

private:
    WiFiClient wifi_client;
    PubSubClient client;
    char topic_status[128];
    char topic_config[128];
    char topic_refresh[128];
    char topic_ip_config[128];
    char topic_ip_state[128];
    char topic_btn_random[128];
    char device_id[64];
    char device_name[64];
    bool _refresh_pending = false;

    void publish_discovery();
    static void on_message(char *topic, byte *payload, unsigned int length);
    static MqttHA *instance;
};

extern MqttHA mqtt;
extern bool mqtt_needs_reconnect;
