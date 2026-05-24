#include "http_server.hpp"
#include "epd_7in3e.hpp"
#include "log.hpp"
#include "mqtt_ha.hpp"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <esp_timer.h>
#include <libb64/cdecode.h>

WebServer server(80);
bool image_uploaded = false;

extern EPD_7IN3E epd;

static Config *p_config = nullptr;

static void handle_root()
{
    String html = R"RAW(<!DOCTYPE html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>E-Ink Album</title>
<style>
body{font-family:system-ui,sans-serif;max-width:480px;margin:0 auto;padding:16px;background:#1a1a2e;color:#e0e0e0}
h1{text-align:center;color:#e94560}
label{display:block;margin-top:12px;font-size:14px;color:#a0a0b0}
input,select{width:100%;padding:10px;margin-top:4px;border:1px solid #333;border-radius:6px;background:#16213e;color:#e0e0e0;box-sizing:border-box}
button{width:100%;padding:12px;margin-top:20px;border:none;border-radius:6px;background:#e94560;color:#fff;font-size:16px;cursor:pointer}
button.danger{background:#533483;margin-top:8px}
.status{margin-top:16px;padding:12px;border-radius:6px;text-align:center;display:none}
.status.ok{background:#1b5e20;display:block}
.status.err{background:#b71c1c;display:block}
</style></head><body>
<h1>E-Ink Album Config</h1>
<form id="cfg">
<label>WiFi SSID</label><input name="wifi_ssid">
<label>WiFi Password</label><input name="wifi_password" type="password">
<label>MQTT Broker</label><input name="mqtt_broker" placeholder="192.168.x.x">
<label>MQTT Port</label><input name="mqtt_port" type="number" value="1883">
<label>MQTT User</label><input name="mqtt_user">
<label>MQTT Password</label><input name="mqtt_password" type="password">
<label>HA Device Name</label><input name="ha_device_name" value="eink_album">
<button type="submit">Save Config</button>
</form>
<button class="danger" onclick="reboot()">Reboot Device</button>
<div id="msg" class="status"></div>
<script>
function show(m,t){var e=document.getElementById('msg');e.textContent=m;e.className='status '+t;setTimeout(function(){e.className='status'},3000)}
fetch('/api/config').then(r=>r.json()).then(c=>{Object.keys(c).forEach(k=>{var e=document.querySelector('[name='+k+']');if(e){if(e.type==='checkbox')e.checked=c[k];else e.value=c[k]}})})
document.getElementById('cfg').onsubmit=function(e){e.preventDefault();var d=new FormData(e.target),o={};d.forEach((v,k)=>{o[k]=v});fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(o)}).then(r=>r.json()).then(j=>show(j.msg||'Saved','ok')).catch(()=>show('Save failed','err'))}
function reboot(){fetch('/api/reboot',{method:'POST'}).then(r=>r.json()).then(j=>show(j.msg||'Rebooting','ok'))}
</script></body></html>)RAW";
    server.send(200, "text/html", html);
}

static void handle_config_get()
{
    JsonDocument doc;
    doc["wifi_ssid"] = p_config->wifi_ssid;
    doc["wifi_password"] = p_config->wifi_password;
    doc["mqtt_broker"] = p_config->mqtt_broker;
    doc["mqtt_port"] = p_config->mqtt_port;
    doc["mqtt_user"] = p_config->mqtt_user;
    doc["mqtt_password"] = p_config->mqtt_password;
    doc["ha_device_name"] = p_config->ha_device_name;

    String body;
    serializeJson(doc, body);
    server.send(200, "application/json", body);
}

static void handle_config_post()
{
    if (!server.hasArg("plain")) {
        server.send(400, "application/json", R"({"msg":"empty body"})");
        return;
    }

    JsonDocument doc;
    auto err = deserializeJson(doc, server.arg("plain"));
    if (err) {
        server.send(400, "application/json", R"({"msg":"invalid json"})");
        return;
    }

    if (doc.containsKey("wifi_ssid"))             strncpy(p_config->wifi_ssid, doc["wifi_ssid"], sizeof(p_config->wifi_ssid) - 1);
    if (doc.containsKey("wifi_password"))         strncpy(p_config->wifi_password, doc["wifi_password"], sizeof(p_config->wifi_password) - 1);
    if (doc.containsKey("mqtt_broker"))           strncpy(p_config->mqtt_broker, doc["mqtt_broker"], sizeof(p_config->mqtt_broker) - 1);
    if (doc.containsKey("mqtt_port"))             p_config->mqtt_port = doc["mqtt_port"];
    if (doc.containsKey("mqtt_user"))             strncpy(p_config->mqtt_user, doc["mqtt_user"], sizeof(p_config->mqtt_user) - 1);
    if (doc.containsKey("mqtt_password"))         strncpy(p_config->mqtt_password, doc["mqtt_password"], sizeof(p_config->mqtt_password) - 1);
    if (doc.containsKey("ha_device_name"))        strncpy(p_config->ha_device_name, doc["ha_device_name"], sizeof(p_config->ha_device_name) - 1);

    p_config->save();
    LOG("Config saved via HTTP, triggering MQTT reconnect");
    mqtt_needs_reconnect = true;
    server.send(200, "application/json", R"({"msg":"Config saved"})");
}

static void handle_upload()
{
    if (!server.hasArg("plain")) {
        server.send(400, "application/json", R"({"msg":"no data"})");
        return;
    }

    String b64 = server.arg("plain");
    int decoded_len = base64_decode_expected_len(b64.length());
    if (decoded_len != EPD_7IN3E::BUFFER_SIZE) {
        LOG("Upload size mismatch: %d b64 -> %d decoded vs %d", b64.length(), decoded_len, EPD_7IN3E::BUFFER_SIZE);
        server.send(400, "application/json", R"({"msg":"invalid size"})");
        return;
    }

    int written = base64_decode_chars(b64.c_str(), b64.length(), (char *)epd.buffer);
    if (written == EPD_7IN3E::BUFFER_SIZE) {
        image_uploaded = true;
        LOG("Image uploaded, %d bytes decoded", written);
        server.send(200, "application/json", R"({"msg":"OK"})");
    } else {
        LOG("Decode failed: got %d bytes, expected %d", written, EPD_7IN3E::BUFFER_SIZE);
        server.send(400, "application/json", R"({"msg":"decode failed"})");
    }
}

static void handle_reboot()
{
    server.send(200, "application/json", R"({"msg":"Rebooting in 2s..."})");
    delay(500);
    ESP.restart();
}

static void handle_health()
{
    JsonDocument doc;
    doc["uptime_us"] = esp_timer_get_time();
    String body;
    serializeJson(doc, body);
    server.send(200, "application/json", body);
}

void http_server_begin(const Config &cfg)
{
    p_config = const_cast<Config *>(&cfg);

    server.on("/", HTTP_GET, handle_root);
    server.on("/api/config", HTTP_GET, handle_config_get);
    server.on("/api/config", HTTP_POST, handle_config_post);
    server.on("/api/upload", HTTP_POST, handle_upload);
    server.on("/api/reboot", HTTP_POST, handle_reboot);
    server.on("/api/health", HTTP_GET, handle_health);

    server.begin();
    LOG("HTTP server started on port 80");
}
