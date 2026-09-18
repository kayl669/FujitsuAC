/*
  FujitsuAC - ESP32 libary for controlling FujitsuAC through MQTT
  Copyright (c) 2025 Benas Ragauskas. All rights reserved.
  
  Project home: https://github.com/Benas09/FujitsuAC
*/

#pragma once

#include "Config.h"

namespace FujitsuAC {
    const FujitsuProfile FUJITSU_PROFILES[] = {
        {"ARXG09KLLAP", 600.0f, 790.0f, 4.5f},
        {"ASYG14KGTB", 1175.0f, 1350.0f, 4.5f},
        {"ASYG14KMCC", 1220.0f, 1410.0f, 4.5f},
        {"ASYG14KETA", 1220.0f, 1410.0f, 4.5f},
        {"ASTH18KMTD", 1160.0f, 1280.0f, 4.5f},
        {"ASTG18KMTC", 1160.0f, 1280.0f, 4.5f},
        {"ASTG22KMTC", 1580.0f, 1700.0f, 4.5f},
        {"ASTG24KMTC", 2080.0f, 1910.0f, 4.5f},
        {"ASTG34KMTC", 2780.0f, 2840.0f, 4.5f},
        {"ASTH30KMTD", 2490.0f, 2390.0f, 4.5f},
        {"ARYG14LSLAP", 1340.0f, 1490.0f, 4.5f},
        {"ARYG18LSLAP", 1620.0f, 1770.0f, 4.5f},
        {"ARXG14KLLAP", 1280.0f, 1320.0f, 4.5f},
        {"ARXG18KLLAP", 1550.0f, 1620.0f, 4.5f},
        {"ARU24RGLX", 1900.0f, 2060.0f, 4.5f},
        {"ASUH18KPAS", 1440.0f, 1260.0f, 4.5f},
        {"ASGG18KJTA-B", 1410.0f, 1280.0f, 4.5f},
        {"ASUG15LZBS", 1040.0f, 1150.0f, 4.5f},
        {"AMUG36LMAS", 3360.0f, 3850.0f, 4.5f},
    };

    const size_t FUJITSU_PROFILE_COUNT = sizeof(FUJITSU_PROFILES) / sizeof(FUJITSU_PROFILES[0]);

    Config::Config(
        const char *version,
        uart_port_t uartPort,
        int rxPin,
        int txPin,
        int ledWPin,
        int ledRPin,
        int resetButtonPin
    ):
    	_preferences(),
    	_version(version),
        _uartPort(uartPort),
        _rxPin(rxPin),
        _txPin(txPin),
    	_ledWPin(ledWPin),
    	_ledRPin(ledRPin),
        _resetButtonPin(resetButtonPin)
	{
    }

	Config::~Config() {
	    _preferences.end();
	}

    void Config::generateUniqueId() {
        char buf[13];
        snprintf(buf, sizeof(buf), "%012llX", ESP.getEfuseMac());

        String uniqueId = buf;
        uniqueId.toLowerCase();

        _uniqueId = uniqueId;
    }

    void Config::load() {
        this->generateUniqueId();

        _preferences.begin("fujitsu_ac", false);

        _wifiSsid = _preferences.getString("wifi-ssid", "");
        _wifiPw = _preferences.getString("wifi-pw", "");
        _mqttIp = _preferences.getString("mqtt-ip", "");
        _mqttPort = _preferences.getString("mqtt-port", "");
        _mqttUser = _preferences.getString("mqtt-user", "");
        _mqttPw = _preferences.getString("mqtt-pw", "");
        _deviceName = _preferences.getString("device-name", "");
        _otaPw = _preferences.getString("ota-pw", "");
        _protocol = _preferences.getString("protocol", "");
        _clim_profile_id = _preferences.getString("clim-profile-id", "");
        _ledsOn = _preferences.getBool("leds-on", true);
        _wifiSleepEnabled = _preferences.getBool("wifi-sleep", true);
        _lowCpuSpeedEnabled = _preferences.getBool("low-cpu-speed", true);

        this->setLowCpuSpeedEnabled(_lowCpuSpeedEnabled);

        // Defaults for an unknown profile, or one whose electrical data is pending (0 W)
        _standbyPower = 4.5f;
        _nominalCoolPower = 600.0f;
        _nominalHeatPower = 700.0f;

        for (const auto &climProfile: FUJITSU_PROFILES) {
            if (strcmp(climProfile.id, _clim_profile_id.c_str()) == 0) {
                _standbyPower = climProfile.standbyW;

                if (climProfile.coolingPowerW > 0) {
                    _nominalCoolPower = climProfile.coolingPowerW;
                }

                if (climProfile.heatingPowerW > 0) {
                    _nominalHeatPower = climProfile.heatingPowerW;
                }

                return;
            }
        }
    }

    void Config::clear() {
	    _preferences.clear();
	}

	bool Config::isEmpty() {
		return _wifiSsid == "";
	}

	void Config::setValue(const char* key, String value) {
		_preferences.putString(key, value);
	}

    void Config::initIO() {
        if (_ledRPin > 0) {
            ledcAttach(_ledRPin, 12000, 10);
            this->toggleRLed(true);
        }

        if (_ledWPin > 0) {
            ledcAttach(_ledWPin, 12000, 10);
            this->toggleWLed(false);
        }

        pinMode(_rxPin, INPUT_PULLUP);
        pinMode(_txPin, OUTPUT);
        digitalWrite(_txPin, LOW);

        if (_resetButtonPin > 0) {
            pinMode(_resetButtonPin, INPUT_PULLUP);
        }
    }

    void Config::toggleWLed(bool status) {
        if (_ledWPin > 0) {
            ledcWrite(_ledWPin, status ? 1020 : 1023);
        }
    }

    void Config::toggleRLed(bool status) {
        if (_ledRPin > 0) {
            ledcWrite(_ledRPin, status ? 1020 : 1023);
        }
    }

    void Config::setLedsStatus(bool status) {
        if (_ledsOn != status) {
            _ledsOn = status;
            _preferences.putBool("leds-on", status);
        }

        this->toggleWLed(status);
        this->toggleRLed(status);
    }

    bool Config::isLedsOn() {
        return _ledsOn;
    }

    void Config::setWifiSleepEnabled(bool status)
    {
        if (_wifiSleepEnabled != status) {
            _wifiSleepEnabled = status;
            _preferences.putBool("wifi-sleep", status);
        }

        WiFi.setSleep(status);
    }

    bool Config::isWifiSleepEnabled() {
        return _wifiSleepEnabled;
    }

    void Config::setLowCpuSpeedEnabled(bool status)
    {
        if (_lowCpuSpeedEnabled != status) {
            _lowCpuSpeedEnabled = status;
            _preferences.putBool("low-cpu-speed", status);
        }

        uint32_t minFreq = 80;
        uint32_t maxFreq = 240;

        String chip = ESP.getChipModel();

        if (chip == "ESP32-C3") {
            minFreq = 80;
            maxFreq = 160;
        }

        setCpuFrequencyMhz(status ? minFreq : maxFreq);
    }

    bool Config::isLowCpuSpeedEnabled() {
        return _lowCpuSpeedEnabled;
    }
}