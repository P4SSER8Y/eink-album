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
        client.unsubscribe(topic_indicator_cmd);
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
    snprintf(topic_btn_random_cmd, sizeof(topic_btn_random_cmd), "homeassistant/button/%s/random/command", device_id);
    snprintf(topic_indicator_cmd, sizeof(topic_indicator_cmd), "homeassistant/sensor/%s/indicator/command", device_id);
    snprintf(topic_indicator_state, sizeof(topic_indicator_state), "homeassistant/light/%s/indicator/state", device_id);

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
        client.subscribe(topic_indicator_cmd);
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

    // MQTT JSON Light: Indicator LED
    JsonDocument light_doc;
    light_doc["name"] = "Indicator LED";
    light_doc["schema"] = "json";
    light_doc["command_topic"] = topic_indicator_cmd;
    light_doc["state_topic"] = topic_indicator_state;
    light_doc["rgb"] = true;
    light_doc["brightness"] = true;
    light_doc["supported_color_modes"].add("rgb");
    light_doc["effect"] = true;
    JsonArray effects = light_doc["effect_list"].to<JsonArray>();
    effects.add("none");
    effects.add("blink");
    effects.add("fast_blink");
    light_doc["unique_id"] = String(device_id) + "_indicator";
    light_doc["device"] = doc["device"];

    String light_payload;
    serializeJson(light_doc, light_payload);

    char topic_light_config[128];
    snprintf(topic_light_config, sizeof(topic_light_config), "homeassistant/light/%s/indicator/config", device_id);

    publish_indicator_state(false, 0, 0, 0);
    if (client.publish(topic_light_config, light_payload.c_str(), true)) {
        LOG("HA discovery: indicator LED (%d bytes)", light_payload.length());
    } else {
        LOG("HA discovery FAILED: indicator LED (%d bytes)", light_payload.length());
    }
}

void MqttHA::publish_random()
{
    if (!client.connected()) return;
    if (client.publish(topic_btn_random_cmd, "PRESS", true)) {
        LOG("MQTT triggered: random button");
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
    if (!instance) return;
    String msg((const char *)payload, length);
    LOG("MQTT recv: %s -> %s", topic, msg.c_str());

    if (strcmp(topic, instance->topic_refresh) == 0) {
        instance->_refresh_pending = true;
    } else if (strcmp(topic, instance->topic_indicator_cmd) == 0) {
        instance->parse_indicator_command(payload, length);
    }
}

void MqttHA::parse_indicator_command(byte *payload, unsigned int length)
{
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload, length);
    if (err) {
        LOG("MQTT indicator: JSON parse error: %s", err.c_str());
        return;
    }

    const char *state = doc["state"] | "";
    if (strcmp(state, "OFF") == 0) {
        IndicatorCommand cmd;
        cmd.pending = true;
        cmd.clear = true;
        _indicator_cmd = cmd;
        LOG("MQTT indicator: OFF");
        return;
    }

    IndicatorCommand cmd;
    cmd.pending = true;

    // Parse color: "0xRRGGBB" (extended) or {"r":...,"g":...,"b":...} (HA standard)
    if (doc["color"].is<const char*>()) {
        const char *color_str = doc["color"];
        uint32_t hex = strtoul(color_str, nullptr, 16);
        cmd.r = (hex >> 16) & 0xFF;
        cmd.g = (hex >> 8) & 0xFF;
        cmd.b = hex & 0xFF;
    } else if (doc["color"].is<JsonObject>()) {
        JsonObject c = doc["color"];
        cmd.r = c["r"] | 255;
        cmd.g = c["g"] | 255;
        cmd.b = c["b"] | 255;
    }

    // Brightness (HA standard)
    if (doc.containsKey("brightness")) {
        float b = doc["brightness"].as<float>() / 255.0f;
        cmd.r = (uint8_t)(cmd.r * b);
        cmd.g = (uint8_t)(cmd.g * b);
        cmd.b = (uint8_t)(cmd.b * b);
    }

    // Frequency: explicit field wins, then map from HA effect
    if (doc.containsKey("frequency")) {
        cmd.freq_hz = doc["frequency"].as<float>();
    } else {
        const char *effect = doc["effect"] | "";
        if (strcmp(effect, "blink") == 0)
            cmd.freq_hz = 1;
        else if (strcmp(effect, "fast_blink") == 0)
            cmd.freq_hz = 4;
        else
            cmd.freq_hz = 0;
    }

    // Timeout: explicit field wins, otherwise default based on mode
    if (doc.containsKey("timeout")) {
        cmd.timeout_s = doc["timeout"];
    } else {
        cmd.timeout_s = (cmd.freq_hz == 0) ? 3600 : 60;
    }

    _indicator_cmd = cmd;
    LOG("MQTT indicator: r=%d g=%d b=%d freq=%d timeout=%d",
        cmd.r, cmd.g, cmd.b, cmd.freq_hz, cmd.timeout_s);
}

void MqttHA::publish_indicator_state(bool on, uint8_t r, uint8_t g, uint8_t b)
{
    if (!client.connected()) return;

    JsonDocument doc;
    if (on) {
        doc["state"] = "ON";
        doc["color_mode"] = "rgb";
        JsonObject color = doc["color"].to<JsonObject>();
        color["r"] = r;
        color["g"] = g;
        color["b"] = b;
    } else {
        doc["state"] = "OFF";
    }

    String payload;
    serializeJson(doc, payload);
    client.publish(topic_indicator_state, payload.c_str(), true);
}
