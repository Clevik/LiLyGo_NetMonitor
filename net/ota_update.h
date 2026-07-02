#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

enum class OtaUpdateState : uint8_t {
  Idle,
  Prepared,
  FsReceiving,
  FsReady,
  FirmwareReceiving,
  ReadyToCommit,
  Failed,
  RebootPending,
};

bool otaUpdateStart(const JsonDocument &request, String &error);
bool otaUpdateBeginUpload(const String &type, size_t total, String &error);
bool otaUpdateWrite(const String &type,
                    size_t index,
                    const uint8_t *data,
                    size_t len,
                    bool final,
                    String &error);
bool otaUpdateCommit(String &error);
void otaUpdateCancel();
bool otaUpdateManualRollback(String &error);
void otaUpdateLoop();

void otaUpdateAddStatus(JsonDocument &doc);
OtaUpdateState otaUpdateState();
const String &otaUpdateLastError();
bool otaUpdateBusy();

bool otaUpdateTelemetryPauseRequested();
void otaUpdateSetTelemetryPaused(bool paused);
bool otaUpdateTelemetryResumeRequested();
void otaUpdateSetTelemetryResumed();
