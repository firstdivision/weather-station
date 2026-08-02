#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include "gps_reader.h"

class WeatherStationApp {
public:
    void begin();
    void tick();

private:
    void updateHeartbeat();
    void reportGpsIfDue();
    void loadWiFiCredentials();
    void saveWiFiCredentials(const String &ssid, const String &password);
    bool tryConnectStation();
    void startConfigAccessPoint();
    void stopConfigAccessPoint();
    void ensureWebServerRunning();
    void maintainWiFi();
    void reportWiFiStatusIfDue();
    void serviceConfigPortal();
    void handleConfigClient(WiFiClient &client);
    void writeHttpHeaders(WiFiClient &client, const char *contentType);
    void writeCorsHeaders(WiFiClient &client);
    void writeIdentityResponse(WiFiClient &client);
    void writeWifiSetupPage(WiFiClient &client);
    void writeStatusPage(WiFiClient &client);
    static String urlDecode(const String &value);
    static void print2Digits(Print &out, uint8_t value);
    static void printGpsStatus(Print &out, const GpsSnapshot &snapshot);

    GpsReader gps_;
    unsigned long lastReportMs_ = 0;
    unsigned long lastBlinkMs_ = 0;
    unsigned long lastWifiAttemptMs_ = 0;
    unsigned long lastWifiStatusMs_ = 0;
    bool apModeEnabled_ = false;
    bool hasWifiCredentials_ = false;
    bool webServerRunning_ = false;
    bool hasTemperature_ = false;
    bool temperatureConversionInProgress_ = false;
    bool ledOn_ = false;
    unsigned long lastTemperatureRequestMs_ = 0;
    unsigned long lastTemperatureReadMs_ = 0;
    float lastTempC_ = 0.0f;
    char wifiSsid_[33] = {0};
    char wifiPassword_[65] = {0};
    WiFiServer configServer_{80};
};
