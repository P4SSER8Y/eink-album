#include "mqtt_ha.hpp"
#include "log.hpp"
#include <Arduino.h>

MqttHA *MqttHA::instance = nullptr;
MqttHA mqtt;
bool mqtt_needs_reconnect = false;

void MqttHA::begin(const Config &cfg)
{
    LOG("MQTT begin: broker='%s' port=%d device='%s'", cfg.mqtt_broker, cfg.mqtt_port, cfg.ha_device_name);

    if (cfg.mqtt_broker[0] == '\0') {
        LOG("MQTT broker not configured, skipping");
        return;
    }

    if (client.connected()) {
        client.unsubscribe(topic_refresh);
        client.disconnect();
    }
    if (wifi_client.connected()) {
        wifi_client.stop();
    }

    strncpy(device_id, cfg.ha_device_name, sizeof(device_id) - 1);
    strncpy(device_name, cfg.ha_device_name, sizeof(device_name) - 1);

    snprintf(topic_config, sizeof(topic_config), "homeassistant/sensor/%s/status/config", device_id);
    snprintf(topic_status, sizeof(topic_status), "homeassistant/sensor/%s/status/state", device_id);
    snprintf(topic_refresh, sizeof(topic_refresh), "homeassistant/sensor/%s/refresh/set", device_id);
    snprintf(topic_ip_config, sizeof(topic_ip_config), "homeassistant/sensor/%s/ip/config", device_id);
    snprintf(topic_ip_state, sizeof(topic_ip_state), "homeassistant/sensor/%s/ip/state", device_id);
    snprintf(topic_btn_random, sizeof(topic_btn_random), "homeassistant/button/%s/random/config", device_id);

    client.setClient(wifi_client);
    client.setServer(cfg.mqtt_broker, cfg.mqtt_port);

    instance = this;
    client.setCallback(on_message);

    char id[64];
    snprintf(id, sizeof(id), "eink_album_%06x", (unsigned)(ESP.getEfuseMac() & 0xFFFFFF));
    if (cfg.mqtt_user[0] != '\0') {
        client.connect(id, cfg.mqtt_user, cfg.mqtt_password);
    } else {
        client.connect(id);
    }

    if (client.connected()) {
        LOG("MQTT connected to %s:%d", cfg.mqtt_broker, cfg.mqtt_port);
        publish_discovery();
        client.subscribe(topic_refresh);
    } else {
        LOG("MQTT connect failed, state=%d", client.state());
    }
}

void MqttHA::loop()
{
    if (client.connected()) {
        client.loop();
    }
}

void MqttHA::publish_status(Status st)
{
    if (!client.connected()) {
        LOG("MQTT publish skipped: not connected");
        return;
    }

    const char *state_str;
    switch (st) {
    case Idle:     state_str = "idle";     break;
    case Fetching: state_str = "fetching"; break;
    case Updating: state_str = "updating"; break;
    case Done:     state_str = "done";     break;
    case Error:    state_str = "error";    break;
    case Restarted:     state_str = "restarted";     break;
    }
    if (client.publish(topic_status, state_str, true)) {
        LOG("MQTT published: %s -> %s", topic_status, state_str);
    } else {
        LOG("MQTT publish FAILED for %s", topic_status);
    }
}

void MqttHA::publish_discovery()
{
    JsonDocument doc;
    doc["name"] = "Status";
    doc["state_topic"] = topic_status;
    doc["icon"] = "mdi:image-frame";
    doc["unique_id"] = String(device_id) + "_status";
    doc["entity_category"] = "diagnostic";

    JsonObject dev = doc["device"].to<JsonObject>();
    dev["identifiers"].add(device_id);
    dev["name"] = device_name;
    dev["model"] = "ESP32-S3 + 7.3\" E-Ink";
    dev["manufacturer"] = "DIY";

    String payload;
    serializeJson(doc, payload);
    if (client.publish(topic_config, payload.c_str(), true)) {
        LOG("HA discovery: status -> %s (%d bytes)", topic_config, payload.length());
    } else {
        LOG("HA discovery FAILED: status (%d bytes)", payload.length());
    }

    // IP address sensor
    JsonDocument ip_doc;
    ip_doc["name"] = "IP Address";
    ip_doc["state_topic"] = topic_ip_state;
    ip_doc["icon"] = "mdi:ip-network";
    ip_doc["unique_id"] = String(device_id) + "_ip";
    ip_doc["entity_category"] = "diagnostic";
    ip_doc["device"] = doc["device"];  // same device

    String ip_payload;
    serializeJson(ip_doc, ip_payload);
    if (client.publish(topic_ip_config, ip_payload.c_str(), true)) {
        LOG("HA discovery: ip -> %s (%d bytes)", topic_ip_config, ip_payload.length());
    } else {
        LOG("HA discovery FAILED: ip (%d bytes)", ip_payload.length());
    }

    // Button: Random
    JsonDocument btn_random;
    btn_random["name"] = "Random";
    btn_random["command_topic"] = "homeassistant/button/" + String(device_id) + "/random/command";
    btn_random["unique_id"] = String(device_id) + "_btn_random";
    btn_random["icon"] = "mdi:shuffle-variant";
    btn_random["device"] = doc["device"];

    String random_payload;
    serializeJson(btn_random, random_payload);
    if (client.publish(topic_btn_random, random_payload.c_str(), true)) {
        LOG("HA discovery: btn random -> %s (%d bytes)", topic_btn_random, random_payload.length());
    } else {
        LOG("HA discovery FAILED: btn random (%d bytes)", random_payload.length());
    }
}

void MqttHA::publish_ip(const char *ip)
{
    if (!client.connected()) return;
    if (client.publish(topic_ip_state, ip, true)) {
        LOG("MQTT published: %s -> %s", topic_ip_state, ip);
    }
}

void MqttHA::on_message(char *topic, byte *payload, unsigned int length)
{
    if (instance) {
        String msg((const char *)payload, length);
        LOG("MQTT recv: %s -> %s", topic, msg.c_str());
        instance->_refresh_pending = true;
    }
}
