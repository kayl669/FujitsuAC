/*
  FujitsuAC - ESP32 libary for controlling FujitsuAC through MQTT
  Copyright (c) 2025 Benas Ragauskas. All rights reserved.
  
  Project home: https://github.com/Benas09/FujitsuAC
*/
#pragma once
#include "FujitsuAC.h"
#include "TFSXW1Bridge.h"

#define VERSION "1.4.8"

RTC_NOINIT_ATTR bool isFallbackAp;
RTC_NOINIT_ATTR int fallbackApReason;

namespace FujitsuAC {
    enum FallbackApReason: int {
        None = 0,
        UnableToConnectWiFi = 1,
        UnableToConnectMqtt = 2,
        ResetReasonPanic = 3,
    };

    FujitsuAC::FujitsuAC(
        uart_port_t uartPort,
        int rxPin,
        int txPin,
        int ledWPin,
        int ledRPin,
        int resetButtonPin
    ):
        _config(VERSION, uartPort, rxPin, txPin, ledWPin, ledRPin, resetButtonPin),
        server(80),
        espClient(),
        _mqttClient(espClient)
    {}

    void FujitsuAC::setup() {
        _config.load();
        _config.initIO();
        littleFsReady = LittleFS.begin(true);
        if (!littleFsReady) {
            Serial.println("[WEB] LittleFS mount failed");
        }

        this->setupWebServer();
        this->handleResetButton();

        if (this->createAP()) {
            this->setupOTA();

            return;
        }

        this->connectToWifi();
        this->server.begin();
        Serial.println("[WEB] HTTP server started on port 80");
        this->setupOTA();

        _mqttClient.setServer(_config.getMqttIp().c_str(), (uint16_t) _config.getMqttPort().toInt());
        _mqttClient.setBufferSize(2048);
    }

    void FujitsuAC::setupOTA() {
        String password = _config.getOtaPw();

        if (password.isEmpty()) {
            password = "faircon";
        }

        ArduinoOTA.setHostname(_config.getDeviceName().c_str());
        ArduinoOTA.setPassword(password.c_str());
        ArduinoOTA.begin();
    }

    void FujitsuAC::loop() {
        this->handleResetButton();

        if (this->isAPState()) {
            ArduinoOTA.handle();

            server.handleClient();

            if (isFallbackAp && millis() - fallbackApCreatedAt > 300000) {
                isFallbackAp = false;
                fallbackApReason = FallbackApReason::None;

                ESP.restart();
            }

            return;
        }

        if (WiFi.status() != WL_CONNECTED) {
            ESP.restart();
        }

        ArduinoOTA.handle();

        server.handleClient();

        connectToMqtt();
        _mqttClient.loop();
        this->bridge->loop();
    }

    void FujitsuAC::clearConfig() {
        _config.clear();

        isFallbackAp = false;
        fallbackApReason = FallbackApReason::None;

        delay(1000);
        ESP.restart();
    }

    String FujitsuAC::getConfigValue(String qs, String key) {
        int start = qs.indexOf(key + "=");
        if (start == -1) return "";

        start += key.length() + 1;
        int end = qs.indexOf("&", start);
        if (end == -1) end = qs.length();

        return this->urlDecode(qs.substring(start, end));
    }

    String FujitsuAC::urlDecode(const String &s) {
        String out;
        out.reserve(s.length());

        for (int i = 0; i < s.length(); i++) {
            char c = s[i];

            if (c == '+') {
                out += ' ';
            } else if (c == '%' && i + 2 < s.length()) {
                char h1 = s[i + 1];
                char h2 = s[i + 2];

                int v1 = isdigit(h1) ? h1 - '0' : toupper(h1) - 'A' + 10;
                int v2 = isdigit(h2) ? h2 - '0' : toupper(h2) - 'A' + 10;

                out += char((v1 << 4) | v2);
                i += 2;
            } else {
                out += c;
            }
        }

        return out;
    }

    void FujitsuAC::parseConfig(String content) {
        _config.setValue("wifi-ssid", getConfigValue(content, "wifi-ssid"));
        _config.setValue("wifi-pw", getConfigValue(content, "wifi-pw"));
        _config.setValue("mqtt-ip", getConfigValue(content, "mqtt-ip"));
        _config.setValue("mqtt-port", getConfigValue(content, "mqtt-port"));
        _config.setValue("mqtt-user", getConfigValue(content, "mqtt-user"));
        _config.setValue("mqtt-pw", getConfigValue(content, "mqtt-pw"));
        _config.setValue("device-name", getConfigValue(content, "device-name"));
        _config.setValue("ota-pw", getConfigValue(content, "ota-pw"));
        _config.setValue("protocol", getConfigValue(content, "protocol"));
        _config.setValue("clim-profile-id", getConfigValue(content, "clim-profile-id"));

        isFallbackAp = false;
        fallbackApReason = FallbackApReason::None;
    }

    void FujitsuAC::handleResetButton() {
        if (_config.getResetButtonPin() > 0 && LOW == digitalRead(_config.getResetButtonPin())) {
            this->clearConfig();
        }
    }

    bool FujitsuAC::isAPState() {
        return isFallbackAp || _config.isEmpty();
    }

    bool FujitsuAC::createAP() {
        if (ESP_RST_POWERON == esp_reset_reason()) {
            isFallbackAp = false;
            fallbackApReason = FallbackApReason::None;
        } else if (ESP_RST_PANIC == esp_reset_reason()) {
            isFallbackAp = true;
            fallbackApReason = FallbackApReason::ResetReasonPanic;
        }

        if (!this->isAPState()) {
            return false;
        }

        IPAddress apIP(192,168,1,1);
        IPAddress apGateway(192,168,1,1);
        IPAddress apSubnet(255,255,255,0);

        WiFi.softAPConfig(apIP, apGateway, apSubnet);

        char accessPointName[64];
        snprintf(accessPointName, sizeof(accessPointName), "faircon-%s", _config.getUniqueId().c_str());

        if (!WiFi.softAP(accessPointName)) {
            ESP.restart();
        }

        this->server.begin();

        if (isFallbackAp) {
            fallbackApCreatedAt = millis();
        }

        return true;
    }

    void FujitsuAC::connectToWifi()
    {
        if (WiFi.status() == WL_CONNECTED) {
            return;
        }

        uint32_t start = millis();

        WiFi.disconnect(true, true);
        WiFi.setHostname(_config.getDeviceName().c_str());
        WiFi.mode(WIFI_STA);

        _config.setWifiSleepEnabled(_config.isWifiSleepEnabled());

        int bestNetwork = -1;
        int bestRSSI = -1000;

        int networkCount = WiFi.scanNetworks();

        for (int i = 0; i < networkCount; i++) {
            uint8_t* bssid = WiFi.BSSID(i);
            int rssi = WiFi.RSSI(i);

            if (
                WiFi.SSID(i) == _config.getWifiSsid()
                && rssi > bestRSSI
            ) {
                bestRSSI = rssi;
                bestNetwork = i;
            }
        }

        if (-1 != bestNetwork) {
            uint8_t* bestBssid = WiFi.BSSID(bestNetwork);
            int channel = WiFi.channel(bestNetwork);

            WiFi.begin(_config.getWifiSsid(), _config.getWifiPw(), channel, bestBssid, true);
        } else {
            WiFi.begin(_config.getWifiSsid(), _config.getWifiPw());
        }

        while (WiFi.status() != WL_CONNECTED) {
            this->handleResetButton();

            _config.toggleRLed(false);
            delay(500);

            _config.toggleRLed(true);
            delay(500);

            if (millis() - start > 60000) {
                isFallbackAp = true;
                fallbackApReason = FallbackApReason::UnableToConnectWiFi;

                ESP.restart();
            }
        }

        Serial.println("\n[Mode Station] Connected to local network.");
        if (MDNS.begin(_config.getDeviceName().c_str())) {
            Serial.printf("[mDNS] http://%s.local\n", _config.getDeviceName().c_str());
            MDNS.addService("http", "tcp", 80);
        }
    }

    void FujitsuAC::connectToMqtt() {
        if (_mqttClient.connected()) {
            return;
        }

        //show that Wifi is still connected
        _config.toggleRLed(true);

        uint32_t start = millis();

        while (!_mqttClient.connected()) {
            this->handleResetButton();

            if (WiFi.status() != WL_CONNECTED) {
                ESP.restart();
            }

            _config.toggleWLed(false);

            char topic[96];
            snprintf(topic, sizeof(topic), "fujitsu/%s/status", _config.getUniqueId().c_str());

            bool connected = false;

            if (_config.getMqttUser() == "") {
                connected = _mqttClient.connect(_config.getDeviceName().c_str(), topic, 0, true, "offline");
            } else {
                connected = _mqttClient.connect(_config.getDeviceName().c_str(), _config.getMqttUser().c_str(),
                                                _config.getMqttPw().c_str(), topic, 0, true, "offline");
            }

            if (connected) {
                _config.toggleWLed(true);

                if (nullptr == bridge) {
                    if (_config.getProtocol() == "UTY-TFSXJ4") {
                        // bridge = new TFSXJ4Bridge(_config, mqttClient);
                        // bridge->setup();
                    } else {
                        bridge = new TFSXW1Bridge(_config, _mqttClient);
                        bridge->setup();
                    }
                } else {
                    bridge->configureMqtt();
                }
            } else {
                _config.toggleWLed(true);

                if (millis() - start > 60000) {
                    isFallbackAp = true;
                    fallbackApReason = FallbackApReason::UnableToConnectMqtt;

                    ESP.restart();
                }

                delay(1000);
            }
        }
    }

    String FujitsuAC::loadResource(const char *path) {
        if (!littleFsReady || !LittleFS.exists(path))return "";
        File f = LittleFS.open(path, "r");
        if (!f)return "";
        String s = f.readString();
        f.close();
        return s;
    }

    void FujitsuAC::sendResource(const char *path, const char *type) {
        String s = loadResource(path);
        if (s.isEmpty()) {
            server.send(404, "text/plain", "Resource not found");
            return;
        }
        server.send(200, type, s);
    }

    String FujitsuAC::jsonEscape(const String &v) const {
        String s = v;
        s.replace("\\", "\\\\");
        s.replace("\"", "\\\"");
        s.replace("\n", "\\n");
        return s;
    }

    String FujitsuAC::profilesHtml() const {
        String h;
        for (size_t i = 0; i < FUJITSU_PROFILE_COUNT; i++) {
            h += "<option value=\"" + String(FUJITSU_PROFILES[i].id) + "\">" + String(FUJITSU_PROFILES[i].id) + " — ";
            if (FUJITSU_PROFILES[i].coolingPowerW > 0 || FUJITSU_PROFILES[i].heatingPowerW > 0)
                h += String(FUJITSU_PROFILES[i].coolingPowerW, 0) + " / " + String(FUJITSU_PROFILES[i].heatingPowerW, 0) + " W";
            else h += "electrical data pending";
            h += "</option>";
        }
        return h;
    }

    String FujitsuAC::renderResource(const char *path) {
        String s = loadResource(path);
        s.replace("{{WIFI_SSID}}", jsonEscape(_config.getWifiSsid()));
        s.replace("{{WIFI_PW}}", jsonEscape(_config.getWifiPw()));
        s.replace("{{MQTT_IP}}", jsonEscape(_config.getMqttIp()));
        s.replace("{{MQTT_PORT}}", jsonEscape(_config.getMqttPort()));
        s.replace("{{MQTT_USER}}", jsonEscape(_config.getMqttUser()));
        s.replace("{{MQTT_PW}}", jsonEscape(_config.getMqttPw()));
        s.replace("{{DEVICE_NAME}}", jsonEscape(_config.getDeviceName()));
        s.replace("{{OTA_PW}}", jsonEscape(_config.getOtaPw()));
        s.replace("{{PROFILES}}", profilesHtml());
        return s;
    }

    String FujitsuAC::getStoredOtaPassword() {
        Preferences prefs;
        String password;
        if (prefs.begin("fujitsu_ac", true)) {
            password = prefs.getString("ota-pw", "");
            prefs.end();
        }

        // Same default value as the FujitsuAC library.
        if (password.isEmpty()) {
            password = "faircon";
        }
        return password;
    }

    bool FujitsuAC::checkOtaWebAuth() {
        String password = getStoredOtaPassword();
        if (!server.authenticate("admin", password.c_str())) {
            server.requestAuthentication(BASIC_AUTH, "Fujitsu OTA", "Authentication required");
            return false;
        }
        return true;
    }

    void FujitsuAC::handleRoot() {
        if (isAPState()) {
            handleConfigGet();
            return;
        }
        sendResource("/index.html", "text/html; charset=utf-8");
    }

    void FujitsuAC::handleApiStatus() {
        String json = "{";
        json += "\"type\": \"" + _config.getClimProfileId() + "\",";
        json += "\"communication\": " + String(bridge->isCommunicationOk() ? "true" : "false") + ",";
        json += "\"mode\":\"" + (bridge ? jsonEscape(bridge->getMode()) : "Auto") + "\",";
        json += "\"target\":" + String(bridge ? bridge->getTargetTemp() : 0, 1) + ",";
        json += "\"room\":" + String(bridge ? bridge->getRoomTemp() : 0, 1) + ",";
        json += "\"fan\":\"" + (bridge ? jsonEscape(bridge->getFan()) : "Auto") + "\",";
        json += "\"power_w\":" + String(bridge ? bridge->getPowerWatts() : 0, 1) + ",";
        json += "\"energy_kwh\":" + String(bridge ? bridge->getEnergyKwh() : 0, 4) + ",";
        json += "\"mqtt\":" + String(_mqttClient.connected() ? "true" : "false");
        json += "}";
        server.send(200, "application/json; charset=utf-8", json);
    }

    void FujitsuAC::handleAction() {
        if (!bridge) {
            server.send(503, "text/plain", "Protocol bridge not ready");
            return;
        }
        String action = server.arg("cmd"), value = server.arg("value");
        if (action == "TEMP_UP" || action == "TEMP_DOWN") {
            float t = bridge->getTargetTemp() + (action == "TEMP_UP" ? 0.5f : -0.5f);
            t = constrain(t, 18.0f, 30.0f);
            bridge->handleMqttCommand("temp", String(t, 1).c_str());
        } else if (action == "MODE" && (value == "off" || value == "auto" || value == "cool" || value == "dry" || value == "fan_only" || value == "heat")) {
            bridge->handleMqttCommand("mode", value.c_str());
        } else if (action == "FAN" && (value == "auto" || value == "quiet" || value == "low" || value == "medium" ||
                                       value == "high")) {
            bridge->handleMqttCommand("fan", value.c_str());
        }
        server.send(204, "text/plain", "");
    }

    void FujitsuAC::handleConfigGet() {
        String s = renderResource("/config.html");
        if (s.isEmpty()) {
            server.send(500, "text/plain", "Web resources unavailable");
            return;
        }
        s.replace("{{NOTICE}}", "");
        server.send(200, "text/html; charset=utf-8", s);
    }

    void FujitsuAC::handleConfigPost() {
        String content;
        for (uint8_t i = 0; i < server.args(); i++) {
            if (i)content += '&';
            content += server.argName(i);
            content += '=';
            content += server.arg(i);
        }
        parseConfig(content);
        String s = renderResource("/config.html");
        s.replace("{{NOTICE}}", "<div class=\"notice success\">Configuration saved. Restarting…</div>");
        server.send(200, "text/html; charset=utf-8", s);
        delay(1000);
        ESP.restart();
    }

    void FujitsuAC::handleRestart() {
        if (!checkOtaWebAuth()) {
            return;
        }
        String s = renderResource("/message.html");
        s.replace("{{TITLE}}", "ESP32 restart");
        s.replace("{{MESSAGE}}", "The board is restarting. Automatic reconnection in:");
        s.replace("{{COUNT}}", "10");
        server.send(200, "text/html; charset=utf-8", s);
        delay(1500);
        ESP.restart();
    }

    void FujitsuAC::handleClearConfig() {
        if (!checkOtaWebAuth()) {
            return;
        }

        String s = renderResource("/message.html");
        s.replace("{{TITLE}}", "ESP32 cleared");
        s.replace("{{MESSAGE}}", "The board is restarting with no configuration.");
        s.replace("{{COUNT}}", "0");
        server.send(200, "text/html; charset=utf-8", s);
        clearConfig();
    }

    void FujitsuAC::setupWebServer() {
        server.on("/", HTTP_GET, [this]() { handleRoot(); });
        server.on("/style.css", HTTP_GET, [this]() { sendResource("/style.css", "text/css; charset=utf-8"); });
        server.on("/app.js", HTTP_GET, [this]() { sendResource("/app.js", "application/javascript; charset=utf-8"); });
        server.on("/api/status", HTTP_GET, [this]() { handleApiStatus(); });
        server.on("/action", HTTP_POST, [this]() { handleAction(); });
        server.on("/action", HTTP_GET, [this]() { handleAction(); });
        server.on("/config", HTTP_GET, [this]() { handleConfigGet(); });
        server.on("/config", HTTP_POST, [this]() { handleConfigPost(); });
        server.on("/restart", HTTP_GET, [this]() { handleRestart(); });
        server.on("/clearConfig", HTTP_GET, [this]() { handleClearConfig(); });
        server.onNotFound([this]() { server.send(404, "text/plain", "Not found"); });
    }
}
