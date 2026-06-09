#pragma once

#include "../display/ui.h"

void telemetryStart(const char *pingHost, uint32_t intervalSec);
void telemetryStop();
Telemetry telemetrySnapshot();
