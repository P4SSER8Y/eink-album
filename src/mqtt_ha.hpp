#pragma once
#include "config.hpp"
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <WiFi.h>

struct IndicatorCommand {
    bool pending = false;
    bool clear = false;
    uint8_t r = 255, g = 255, b = 255;
    float freq_hz = 0;
    uint32_t timeout_s = 0;
};

class MqttHA {
public:
    enum Status { Idle, Fetching, Updating, Done, Error, Restarted };

    void begin(const Config &cfg);
    void loop();
    void publish_status(Status st);
    void publish_ip(const char *ip);
    void publish_random();
    bool is_connected() { return client.connected(); }
    bool refresh_requested() { bool v = _refresh_pending; _refresh_pending = false; return v; }
    bool indicator_cmd_pending() { return _indicator_cmd.pending; }
    IndicatorCommand consume_indicator_cmd() {
        IndicatorCommand cmd = _indicator_cmd;
        _indicator_cmd.pending = false;
        return cmd;
    }
    void publish_indicator_state(bool on, uint8_t r, uint8_t g, uint8_t b);

private:
    WiFiClient wifi_client;
    PubSubClient client;
    char topic_status[128];
    char topic_config[128];
    char topic_refresh[128];
    char topic_ip_config[128];
    char topic_ip_state[128];
    char topic_btn_random[128];
    char topic_btn_random_cmd[128];
    char topic_indicator_cmd[128];
    char topic_indicator_state[128];
    char device_id[64];
    char device_name[64];
    bool _refresh_pending = false;
    IndicatorCommand _indicator_cmd;

    void publish_discovery();
    void parse_indicator_command(byte *payload, unsigned int length);
    static void on_message(char *topic, byte *payload, unsigned int length);
    static MqttHA *instance;
};

extern MqttHA mqtt;
extern bool mqtt_needs_reconnect;
