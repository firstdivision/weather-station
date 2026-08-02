#include "weather_station_app.h"

#include <cstring>

#include <LittleFS.h>
#include <WiFi.h>

#include <OneWire.h>
#include <DallasTemperature.h>


#define ONE_WIRE_BUS 15

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);


namespace {
constexpr uint32_t kGpsBaud = 9600;
constexpr int8_t kGpsRxPin = 1;
constexpr uint32_t kReportIntervalMs = 1000;
constexpr uint32_t kBlinkIntervalMs = 100;
constexpr uint32_t kTemperatureSampleIntervalMs = 2000;
constexpr uint32_t kTemperatureConversionMs = 750;
constexpr uint32_t kWifiConnectTimeoutMs = 15000;
constexpr uint32_t kWifiReconnectIntervalMs = 30000;
constexpr uint32_t kWifiStatusReportIntervalMs = 5000;
constexpr char kAccessPointSsid[] = "WeatherStation-Setup";
constexpr char kAccessPointPassword[] = "configureme";
constexpr char kWifiConfigPath[] = "/wifi.cfg";
constexpr char kCredentialMagic[] = "WSC1";

void printTrimmedRawSentence(Print &out, const char *sentence)
{
    size_t len = std::strlen(sentence);
    while (len > 0 && (sentence[len - 1] == '\r' || sentence[len - 1] == '\n')) {
        --len;
    }

    for (size_t i = 0; i < len; ++i) {
        out.print(sentence[i]);
    }
}
}

void WeatherStationApp::begin()
{
    Serial.begin(115200);
    Serial1.setRX(kGpsRxPin);
    gps_.begin(Serial1, kGpsBaud);

    Serial.println("GPS starting...");

    sensors.begin();
    sensors.setWaitForConversion(false);
    sensors.requestTemperatures();
    temperatureConversionInProgress_ = true;
    lastTemperatureRequestMs_ = millis();
    lastTemperatureReadMs_ = millis();
    Serial.print("Temp sensors found: ");
    Serial.println(sensors.getDeviceCount());

    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, LOW);

    loadWiFiCredentials();
    if (!tryConnectStation()) {
        startConfigAccessPoint();
    }
}

void WeatherStationApp::tick()
{
    maintainWiFi();
    ensureWebServerRunning();

    if (apModeEnabled_ || WiFi.status() == WL_CONNECTED) {
        serviceConfigPortal();
    }

    reportWiFiStatusIfDue();
    updateHeartbeat();
    gps_.update();
    reportGpsIfDue();
}

void WeatherStationApp::ensureWebServerRunning()
{
    const bool shouldBeRunning = apModeEnabled_ || WiFi.status() == WL_CONNECTED;
    if (!shouldBeRunning) {
        if (webServerRunning_) {
            configServer_.stop();
            webServerRunning_ = false;
            Serial.println("Web: server stopped.");
        }
        return;
    }

    if (webServerRunning_) {
        return;
    }

    configServer_.begin();
    webServerRunning_ = true;
    if (apModeEnabled_) {
        Serial.println("Web: setup page available at '/'.");
    } else {
        Serial.println("Web: status page available at '/'.");
    }
}

void WeatherStationApp::reportWiFiStatusIfDue()
{
    const unsigned long now = millis();
    if (now - lastWifiStatusMs_ < kWifiStatusReportIntervalMs) {
        return;
    }

    lastWifiStatusMs_ = now;

    if (apModeEnabled_) {
        Serial.print("WiFi: setup mode (AP '");
        Serial.print(kAccessPointSsid);
        Serial.print("'), IP ");
        Serial.println(WiFi.localIP());
        return;
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.print("WiFi: connected to '");
        Serial.print(WiFi.SSID());
        Serial.print("', IP ");
        Serial.println(WiFi.localIP());
        return;
    }

    Serial.println("WiFi: station disconnected, attempting reconnect.");
}

void WeatherStationApp::loadWiFiCredentials()
{
    if (!LittleFS.begin()) {
        Serial.println("WiFi: LittleFS mount failed, credentials unavailable.");
        return;
    }

    if (!LittleFS.exists(kWifiConfigPath)) {
        Serial.println("WiFi: no stored credentials found.");
        return;
    }

    File file = LittleFS.open(kWifiConfigPath, "r");
    if (!file) {
        Serial.println("WiFi: failed to open credential file.");
        return;
    }

    String magic = file.readStringUntil('\n');
    String ssid = file.readStringUntil('\n');
    String password = file.readStringUntil('\n');
    magic.trim();
    ssid.trim();
    password.trim();
    file.close();

    if (magic != kCredentialMagic || ssid.length() == 0) {
        Serial.println("WiFi: credential file invalid.");
        return;
    }

    ssid.toCharArray(wifiSsid_, sizeof(wifiSsid_));
    password.toCharArray(wifiPassword_, sizeof(wifiPassword_));
    hasWifiCredentials_ = true;
    Serial.print("WiFi: loaded saved SSID '");
    Serial.print(wifiSsid_);
    Serial.println("'.");
}

void WeatherStationApp::saveWiFiCredentials(const String &ssid, const String &password)
{
    if (!LittleFS.begin()) {
        Serial.println("WiFi: LittleFS mount failed, cannot save credentials.");
        return;
    }

    File file = LittleFS.open(kWifiConfigPath, "w");
    if (!file) {
        Serial.println("WiFi: failed to open credential file for write.");
        return;
    }

    file.println(kCredentialMagic);
    file.println(ssid);
    file.println(password);
    if (!file) {
        file.close();
        Serial.println("WiFi: failed to persist credentials.");
        return;
    }
    file.close();

    ssid.toCharArray(wifiSsid_, sizeof(wifiSsid_));
    password.toCharArray(wifiPassword_, sizeof(wifiPassword_));
    hasWifiCredentials_ = true;
    Serial.println("WiFi: credentials saved.");
}

bool WeatherStationApp::tryConnectStation()
{
    if (!hasWifiCredentials_) {
        return false;
    }

    Serial.print("WiFi: connecting to '");
    Serial.print(wifiSsid_);
    Serial.println("'...");

    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(250);
    WiFi.begin(wifiSsid_, wifiPassword_);

    const unsigned long startedMs = millis();
    unsigned long lastDotMs = startedMs;
    while (WiFi.status() != WL_CONNECTED && (millis() - startedMs) < kWifiConnectTimeoutMs) {
        gps_.update();
        delay(25);

        const unsigned long now = millis();
        if (now - lastDotMs >= 250) {
            Serial.print('.');
            lastDotMs = now;
        }
    }
    Serial.println();

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi: station connect failed.");
        return false;
    }

    Serial.print("WiFi: connected, IP ");
    Serial.println(WiFi.localIP());

    return true;
}

void WeatherStationApp::startConfigAccessPoint()
{
    if (apModeEnabled_) {
        return;
    }

    WiFi.mode(WIFI_AP);
    if (!WiFi.beginAP(kAccessPointSsid, kAccessPointPassword)) {
        Serial.println("WiFi: failed to start config AP.");
        return;
    }
    apModeEnabled_ = true;
    Serial.println("WiFi: config AP active.");
    Serial.print("WiFi: join SSID '");
    Serial.print(kAccessPointSsid);
    Serial.print("' (password '");
    Serial.print(kAccessPointPassword);
    Serial.println("').");
    Serial.print("WiFi: open http://");
    Serial.println(WiFi.localIP());
}

void WeatherStationApp::stopConfigAccessPoint()
{
    if (!apModeEnabled_) {
        return;
    }

    configServer_.stop();
    webServerRunning_ = false;
    WiFi.end();
    apModeEnabled_ = false;
    Serial.println("WiFi: config AP stopped.");
}

void WeatherStationApp::maintainWiFi()
{
    if (apModeEnabled_) {
        return;
    }

    if (WiFi.status() == WL_CONNECTED) {
        return;
    }

    if (!hasWifiCredentials_) {
        startConfigAccessPoint();
        return;
    }

    const unsigned long now = millis();
    if ((now - lastWifiAttemptMs_) < kWifiReconnectIntervalMs) {
        return;
    }

    lastWifiAttemptMs_ = now;
    if (!tryConnectStation()) {
        startConfigAccessPoint();
    }
}

void WeatherStationApp::serviceConfigPortal()
{
    WiFiClient client = configServer_.accept();
    if (!client) {
        return;
    }

    handleConfigClient(client);
    delay(5);
    client.stop();
}

void WeatherStationApp::handleConfigClient(WiFiClient &client)
{
    String requestLine;
    unsigned long startedMs = millis();
    while (client.connected() && (millis() - startedMs) < 2000) {
        if (!client.available()) {
            delay(1);
            continue;
        }
        requestLine = client.readStringUntil('\n');
        requestLine.trim();
        break;
    }

    if (requestLine.length() == 0) {
        return;
    }

    const int firstSpace = requestLine.indexOf(' ');
    const int secondSpace = requestLine.indexOf(' ', firstSpace + 1);
    if (firstSpace < 0 || secondSpace < 0) {
        client.println("HTTP/1.1 400 Bad Request");
        client.println("Connection: close");
        client.println();
        return;
    }

    const String method = requestLine.substring(0, firstSpace);
    const String path = requestLine.substring(firstSpace + 1, secondSpace);

    int contentLength = 0;
    while (client.connected()) {
        String header = client.readStringUntil('\n');
        header.trim();
        if (header.length() == 0) {
            break;
        }

        if (header.startsWith("Content-Length:")) {
            String value = header.substring(String("Content-Length:").length());
            value.trim();
            contentLength = value.toInt();
        }
    }

    if (method == "OPTIONS") {
        client.println("HTTP/1.1 204 No Content");
        client.println("X-Weather-Station: pico-weather-station");
        client.print("X-Weather-Station-Mode: ");
        client.println(apModeEnabled_ ? "setup" : "station");
        writeCorsHeaders(client);
        client.println("Connection: close");
        client.println();
        return;
    }

    if (method == "GET" && path == "/identify") {
        writeIdentityResponse(client);
        return;
    }

    const bool isSaveRequest = (method == "POST" && path == "/save");
    if (!isSaveRequest) {
        if (apModeEnabled_) {
            writeWifiSetupPage(client);
        } else {
            writeStatusPage(client);
        }
        return;
    }

    String body;
    if (contentLength > 0) {
        body.reserve(contentLength);
        const unsigned long bodyStartedMs = millis();
        while (body.length() < static_cast<unsigned int>(contentLength) && client.connected()) {
            while (client.available() && body.length() < static_cast<unsigned int>(contentLength)) {
                body += static_cast<char>(client.read());
            }

            if ((millis() - bodyStartedMs) > 2000) {
                break;
            }

            delay(1);
        }
    }

    String ssid;
    String password;
    int cursor = 0;
    while (cursor >= 0 && cursor < body.length()) {
        const int ampersand = body.indexOf('&', cursor);
        const int nextCursor = (ampersand < 0) ? body.length() : ampersand;
        const int equals = body.indexOf('=', cursor);

        if (equals > cursor && equals < nextCursor) {
            const String key = body.substring(cursor, equals);
            const String value = urlDecode(body.substring(equals + 1, nextCursor));
            if (key == "ssid") {
                ssid = value;
            } else if (key == "password") {
                password = value;
            }
        }

        if (ampersand < 0) {
            break;
        }
        cursor = ampersand + 1;
    }

    writeHttpHeaders(client, "text/html; charset=utf-8");

    if (ssid.length() == 0) {
        client.println("<html><body><h1>Missing SSID</h1><p><a href='/'>Go back</a></p></body></html>");
        return;
    }

    saveWiFiCredentials(ssid, password);
    stopConfigAccessPoint();
    const bool connected = tryConnectStation();
    if (!connected) {
        startConfigAccessPoint();
    }

    if (connected) {
        client.println("<html><body><h1>Connected</h1><p>Device joined Wi-Fi successfully.</p></body></html>");
    } else {
        client.println("<html><body><h1>Connect failed</h1><p>Could not join that network. AP mode is still active.</p>"
                       "<p><a href='/'>Try again</a></p></body></html>");
    }
}

void WeatherStationApp::writeWifiSetupPage(WiFiClient &client)
{
    writeHttpHeaders(client, "text/html; charset=utf-8");
    client.println("<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>"
                   "<title>Weather Station Setup</title>"
                   "<style>body{font-family:Verdana,sans-serif;background:#e7f1f7;color:#123;padding:1.5rem;}"
                   "main{max-width:520px;background:#fff;border-radius:12px;padding:1.25rem;box-shadow:0 10px 25px rgba(0,0,0,.08);}"
                   "label{display:block;margin:.8rem 0 .35rem;}input{width:100%;padding:.6rem;border:1px solid #aac;border-radius:8px;}"
                   "button{margin-top:1rem;background:#005f73;color:#fff;border:none;padding:.7rem 1rem;border-radius:8px;cursor:pointer;}"
                   "small{display:block;margin-top:1rem;color:#456;}</style></head><body><main><h1>Wi-Fi Setup</h1>"
                   "<p>Enter your network to move this station into normal mode.</p>"
                   "<form method='POST' action='/save'><label>SSID</label><input name='ssid' maxlength='32' required>"
                   "<label>Password</label><input name='password' maxlength='64' type='password'>"
                   "<button type='submit'>Save and Connect</button></form>"
                   "<small>AP SSID: WeatherStation-Setup</small></main></body></html>");
}

void WeatherStationApp::writeStatusPage(WiFiClient &client)
{
    const GpsSnapshot snapshot = gps_.readSnapshot();

    String location;
    if (snapshot.hasLocation) {
        location = String(snapshot.latitude, 6) + ", " + String(snapshot.longitude, 6);
    } else {
        location = "Waiting for GPS fix";
    }

    String temperature;
    if (hasTemperature_) {
        const float tempF = (lastTempC_ * 9.0f / 5.0f) + 32.0f;
        temperature = String(lastTempC_, 2) + " C / " + String(tempF, 2) + " F";
    } else {
        temperature = "No reading yet";
    }

    writeHttpHeaders(client, "text/html; charset=utf-8");
    client.println("<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>"
                   "<title>Weather Station Status</title>"
                   "<style>body{font-family:Verdana,sans-serif;background:#f2f9f3;color:#163;padding:1.5rem;}"
                   "main{max-width:560px;background:#fff;border-radius:12px;padding:1.25rem;box-shadow:0 10px 25px rgba(0,0,0,.08);}"
                   ".card{background:#eef7ee;padding:.8rem 1rem;border-radius:8px;margin:.8rem 0;}"
                   "h2{margin:.2rem 0 .4rem;}small{color:#486;}</style></head><body><main><h1>Weather Station</h1>");
    client.print("<small>Network: ");
    client.print(WiFi.SSID());
    client.print(" | IP: ");
    client.print(WiFi.localIP());
    client.println("</small>");
    client.print("<div class='card'><h2>Location</h2><p>");
    client.print(location);
    client.println("</p></div>");
    client.print("<div class='card'><h2>Temperature</h2><p>");
    client.print(temperature);
    client.println("</p></div>");
    client.println("</main></body></html>");
}

void WeatherStationApp::writeHttpHeaders(WiFiClient &client, const char *contentType)
{
    client.println("HTTP/1.1 200 OK");
    client.print("Content-Type: ");
    client.println(contentType);
    client.println("X-Weather-Station: pico-weather-station");
    client.print("X-Weather-Station-Mode: ");
    client.println(apModeEnabled_ ? "setup" : "station");
    writeCorsHeaders(client);
    client.println("Connection: close");
    client.println();
}

void WeatherStationApp::writeCorsHeaders(WiFiClient &client)
{
    client.println("Access-Control-Allow-Origin: *");
    client.println("Access-Control-Allow-Methods: GET, POST, OPTIONS");
    client.println("Access-Control-Allow-Headers: Content-Type");
    client.println("Access-Control-Max-Age: 600");
    client.println("Access-Control-Allow-Private-Network: true");
    client.println("Access-Control-Expose-Headers: X-Weather-Station, X-Weather-Station-Mode");
}

void WeatherStationApp::writeIdentityResponse(WiFiClient &client)
{
    writeHttpHeaders(client, "application/json; charset=utf-8");
    client.print("{\"device\":\"weather-station\",\"server\":\"pico\",\"mode\":\"");
    client.print(apModeEnabled_ ? "setup" : "station");
    client.print("\",\"ssid\":\"");
    if (apModeEnabled_) {
        client.print(kAccessPointSsid);
    } else {
        client.print(WiFi.SSID());
    }
    client.print("\",\"ip\":\"");
    client.print(WiFi.localIP());
    client.println("\"}");
}

String WeatherStationApp::urlDecode(const String &value)
{
    String output;
    output.reserve(value.length());

    for (size_t i = 0; i < value.length(); ++i) {
        const char ch = value[i];
        if (ch == '+') {
            output += ' ';
            continue;
        }

        if (ch == '%' && i + 2 < value.length()) {
            const char hi = value[i + 1];
            const char lo = value[i + 2];

            auto hexValue = [](char c) -> int {
                if (c >= '0' && c <= '9') {
                    return c - '0';
                }
                if (c >= 'a' && c <= 'f') {
                    return c - 'a' + 10;
                }
                if (c >= 'A' && c <= 'F') {
                    return c - 'A' + 10;
                }
                return -1;
            };

            const int hiValue = hexValue(hi);
            const int loValue = hexValue(lo);
            if (hiValue >= 0 && loValue >= 0) {
                output += static_cast<char>((hiValue << 4) | loValue);
                i += 2;
                continue;
            }
        }

        output += ch;
    }

    return output;
}

void WeatherStationApp::updateHeartbeat()
{
    const unsigned long now = millis();

    if (now - lastBlinkMs_ >= kBlinkIntervalMs) {
        lastBlinkMs_ = now;
        ledOn_ = !ledOn_;
        digitalWrite(LED_BUILTIN, ledOn_ ? HIGH : LOW);
    }

    if (temperatureConversionInProgress_ && (now - lastTemperatureRequestMs_) >= kTemperatureConversionMs) {
        const float tempC = sensors.getTempCByIndex(0);
        if (tempC == DEVICE_DISCONNECTED_C) {
            hasTemperature_ = false;
            Serial.println("Temperature: sensor disconnected (-127C). Check data pin/pull-up/power/ground.");
        } else {
            hasTemperature_ = true;
            lastTempC_ = tempC;
            const float tempF = (tempC * 9.0f / 5.0f) + 32.0f;
            Serial.print("Temp: ");
            Serial.print(tempC, 2);
            Serial.print("c (");
            Serial.print(tempF, 2);
            Serial.println(" f)");
        }

        temperatureConversionInProgress_ = false;
        lastTemperatureReadMs_ = now;
    }

    if (!temperatureConversionInProgress_ && (now - lastTemperatureReadMs_) >= kTemperatureSampleIntervalMs) {
        sensors.requestTemperatures();
        temperatureConversionInProgress_ = true;
        lastTemperatureRequestMs_ = now;
    }
}

void WeatherStationApp::reportGpsIfDue()
{
    const unsigned long now = millis();
    if (now - lastReportMs_ < kReportIntervalMs) {
        return;
    }

    lastReportMs_ = now;
    const GpsSnapshot snapshot = gps_.readSnapshot();
    printGpsStatus(Serial, snapshot);
    Serial.println();
}

void WeatherStationApp::print2Digits(Print &out, uint8_t value)
{
    if (value < 10) {
        out.print('0');
    }
    out.print(value);
}

void WeatherStationApp::printGpsStatus(Print &out, const GpsSnapshot &snapshot)
{
    if (snapshot.hasLocation) {
        out.print("Lat: ");
        out.print(snapshot.latitude, 6);
        out.print("  Lon: ");
        out.print(snapshot.longitude, 6);
    } else {
        out.print("Lat/Lon: waiting for fix");
    }

    out.print("  Sats: ");
    if (snapshot.hasSatellites) {
        out.print(snapshot.satellites);
    } else {
        out.print("waiting");
    }

    out.print("  Quality: ");
    if (snapshot.hasHdop) {
        out.print(snapshot.quality);
        out.print(" (HDOP ");
        out.print(snapshot.hdop, 2);
        out.print(")");
    } else {
        out.print("waiting");
    }

    out.print("  UTC: ");
    if (snapshot.hasUtc) {
        out.print(snapshot.year);
        out.print('-');
        print2Digits(out, snapshot.month);
        out.print('-');
        print2Digits(out, snapshot.day);
        out.print(' ');
        print2Digits(out, snapshot.hour);
        out.print(':');
        print2Digits(out, snapshot.minute);
        out.print(':');
        print2Digits(out, snapshot.second);
    } else {
        out.print("waiting for time");
    }

    out.print("\n    RAW: ");
    if (snapshot.hasRawSentence) {
        printTrimmedRawSentence(out, snapshot.rawSentence);
    } else {
        out.print("waiting for sentence");
    }
}
