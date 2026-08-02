#include "gps_reader.h"

#include <cstring>

void GpsReader::begin(HardwareSerial &serial, uint32_t baudRate)
{
    serial_ = &serial;
    serial_->begin(baudRate);
}

void GpsReader::update()
{
    if (serial_ == nullptr) {
        return;
    }

    while (serial_->available()) {
        const char c = static_cast<char>(serial_->read());

        if (c == '$') {
            resetCurrentSentence();
            currentSentence_[currentSentenceLen_++] = c;
        } else if (currentSentenceLen_ > 0) {
            if (!sentenceOverflow_ && currentSentenceLen_ < (kSentenceBufferSize - 1)) {
                currentSentence_[currentSentenceLen_++] = c;
            } else {
                sentenceOverflow_ = true;
            }

            if (c == '\n') {
                if (!sentenceOverflow_) {
                    currentSentence_[currentSentenceLen_] = '\0';
                    std::strncpy(lastSentence_, currentSentence_, kSentenceBufferSize);
                    lastSentence_[kSentenceBufferSize - 1] = '\0';
                    hasLastSentence_ = true;
                }
                resetCurrentSentence();
            }
        }

        gps_.encode(c);
    }
}

GpsSnapshot GpsReader::readSnapshot()
{
    GpsSnapshot snapshot;

    if (gps_.location.isValid()) {
        snapshot.hasLocation = true;
        snapshot.latitude = gps_.location.lat();
        snapshot.longitude = gps_.location.lng();
    }

    if (gps_.satellites.isValid()) {
        snapshot.hasSatellites = true;
        snapshot.satellites = gps_.satellites.value();
    }

    if (gps_.hdop.isValid()) {
        const uint32_t hdop = gps_.hdop.value();
        snapshot.hasHdop = true;
        snapshot.hdop = hdop / 100.0f;
        snapshot.quality = qualityFromHdop(hdop);
    }

    if (gps_.time.isValid() && gps_.date.isValid()) {
        snapshot.hasUtc = true;
        snapshot.year = gps_.date.year();
        snapshot.month = gps_.date.month();
        snapshot.day = gps_.date.day();
        snapshot.hour = gps_.time.hour();
        snapshot.minute = gps_.time.minute();
        snapshot.second = gps_.time.second();
    }

    if (hasLastSentence_) {
        snapshot.hasRawSentence = true;
        std::strncpy(snapshot.rawSentence, lastSentence_, sizeof(snapshot.rawSentence));
        snapshot.rawSentence[sizeof(snapshot.rawSentence) - 1] = '\0';
    }

    return snapshot;
}

void GpsReader::resetCurrentSentence()
{
    currentSentenceLen_ = 0;
    sentenceOverflow_ = false;
    currentSentence_[0] = '\0';
}

const char *GpsReader::qualityFromHdop(uint32_t hdopHundredths)
{
    if (hdopHundredths <= 100) {
        return "excellent";
    }
    if (hdopHundredths <= 200) {
        return "good";
    }
    if (hdopHundredths <= 500) {
        return "fair";
    }
    return "poor";
}
