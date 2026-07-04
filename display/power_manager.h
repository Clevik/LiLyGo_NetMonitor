#pragma once

#include <Arduino.h>

#include "settings.h"

void powerManagerBegin(const PowerSaveSettings &settings, uint32_t nowMs);
void powerManagerApplySettings(const PowerSaveSettings &settings,
                               uint32_t nowMs);
void powerManagerTick(uint32_t nowMs);
bool powerManagerHandleActivity(uint32_t nowMs);
void powerManagerCycleManualBrightness(uint32_t nowMs);
