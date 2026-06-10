#pragma once

#include "../settings.h"
#include "../display/ui.h"

bool telemetryStart(const Settings &settings);
bool telemetryStop(uint32_t timeoutMs = 10000);
Telemetry telemetrySnapshot();
