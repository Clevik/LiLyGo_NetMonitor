#pragma once

#include "../settings.h"
#include "../display/ui.h"

void telemetryStart(const Settings &settings);
void telemetryStop();
Telemetry telemetrySnapshot();
