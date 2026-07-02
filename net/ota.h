#pragma once

#include "settings.h"

class AsyncWebServer;

void otaBegin(Settings &settings);
void otaRegisterUpdateRoutes(AsyncWebServer &server);
void otaLoop(const Settings &settings);
void otaEnd();
bool otaSettingsSaved();
bool otaTelemetrySettingsChanged();
