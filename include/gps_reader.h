#pragma once

#include <Arduino.h>
#include <TinyGPS++.h>

struct GpsSnapshot {
    bool hasLocation = false;
    double latitude = 0.0;
    double longitude = 0.0;

    bool hasSatellites = false;
    uint32_t satellites = 0;

    bool hasHdop = false;
    float hdop = 0.0f;
    const char *quality = "waiting";

    bool hasUtc = false;
    uint16_t year = 0;
    uint8_t month = 0;
    uint8_t day = 0;
    uint8_t hour = 0;
    uint8_t minute = 0;
    uint8_t second = 0;

    bool hasRawSentence = false;
    char rawSentence[128] = {0};
};

class GpsReader {
public:
    void begin(HardwareSerial &serial, uint32_t baudRate);
    void update();
    GpsSnapshot readSnapshot();

private:
    static constexpr size_t kSentenceBufferSize = 128;
    static const char *qualityFromHdop(uint32_t hdopHundredths);
    void resetCurrentSentence();

    HardwareSerial *serial_ = nullptr;
    TinyGPSPlus gps_;
    char currentSentence_[kSentenceBufferSize] = {0};
    size_t currentSentenceLen_ = 0;
    bool sentenceOverflow_ = false;
    char lastSentence_[kSentenceBufferSize] = {0};
    bool hasLastSentence_ = false;
};
