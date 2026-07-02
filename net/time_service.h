#pragma once

#include <Arduino.h>

void timeServiceBegin(const String &timeZone);
bool timeServiceSetZone(const String &timeZone);
bool timeServiceIsZoneSupported(const String &timeZone);
bool timeServiceLocalMinute(uint16_t &minuteOfDay);
void timeServicePrintZones(Print &output);
