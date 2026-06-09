#pragma once

#include "settings.h"

void portalBegin(Settings &settings);
void portalLoop();
bool portalConfigSaved();
void portalEnd();
const char *portalGetApName();
