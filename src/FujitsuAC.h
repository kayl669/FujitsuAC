#pragma once

//WiFi
#include <WiFi.h>

// Access point
#include <WebServer.h>
#include <LittleFS.h>
#include <NetworkClient.h>
#include <WiFiAP.h>

//OTA
#include <ESPmDNS.h>
#include <NetworkUdp.h>
#include <ArduinoOTA.h>

//MQTT
#include <PubSubClient.h>

#include <Config.h>
#include <Uart.h>
#include <IMqttBridge.h>

namespace FujitsuAC {

    class FujitsuAC {
    	public:
            FujitsuAC(
                uart_port_t uartPort,
                int rxPin,
                int txPin,
                int ledWPin,
                int ledRPin,
                int resetButtonPin
            );

			void setup();
	        void loop();

            void clearConfig();

        private:
            Config _config;

            WebServer server;

            WiFiClient espClient;
            PubSubClient _mqttClient;

            IMqttBridge* bridge = nullptr;

            uint32_t fallbackApCreatedAt = 0;
            bool webOtaAuthorized = false;
            bool littleFsReady = false;

            String getConfigValue(String qs, String key);
            String urlDecode(const String &s);
            void parseConfig(String content);

            void handleResetButton();

            bool isAPState();
            bool createAP();
            void setupOTA();
            void setupWebServer();

            void connectToWifi();
            void connectToMqtt();
            bool checkOtaWebAuth();
            String getStoredOtaPassword();
            String loadResource(const char *path);
            String renderResource(const char *path);
            void sendResource(const char *path, const char *contentType);
            String profilesHtml() const;
            String jsonEscape(const String &value) const;
            void handleRoot();
            void handleApiStatus();
            void handleAction();
            void handleConfigGet();
            void handleConfigPost();
            void handleRestart();
            void handleClearConfig();
    };
}
