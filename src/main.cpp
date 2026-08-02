#include <Arduino.h>
#include "weather_station_app.h"

WeatherStationApp app;

void setup()
{
    app.begin();
}

void loop()
{
    app.tick();
}