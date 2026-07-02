#include "ota_update.h"

#include <LittleFS.h>
#include <esp_app_format.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <mbedtls/sha256.h>

#include "ota_state.h"

namespace {

constexpr uint32_t OTA_SESSION_TIMEOUT_MS = 120000;

enum class UpdateMode : uint8_t {
  Firmware,
  Bundle,
};

struct Sha256State {
  mbedtls_sha256_context context;
  uint8_t expected[32] = {};
  bool initialized = false;
};

struct UpdateSession {
  OtaUpdateState state = OtaUpdateState::Idle;
  UpdateMode mode = UpdateMode::Firmware;
  const esp_partition_t *appPartition = nullptr;
  const esp_partition_t *fsPartition = nullptr;
  esp_ota_handle_t appHandle = 0;
  bool appHandleOpen = false;
  size_t expectedFirmwareSize = 0;
  size_t expectedFsSize = 0;
  size_t receivedFirmware = 0;
  size_t receivedFs = 0;
  Sha256State firmwareSha;
  Sha256State fsSha;
  String error;
  bool pauseRequested = false;
  bool telemetryPaused = false;
  bool resumeRequested = false;
  uint32_t rebootAtMs = 0;
  uint32_t lastActivityMs = 0;
};

UpdateSession g_session;

const char *stateName(OtaUpdateState state) {
  switch (state) {
    case OtaUpdateState::Idle: return "idle";
    case OtaUpdateState::Prepared: return "prepared";
    case OtaUpdateState::FsReceiving: return "fsReceiving";
    case OtaUpdateState::FsReady: return "fsReady";
    case OtaUpdateState::FirmwareReceiving: return "firmwareReceiving";
    case OtaUpdateState::ReadyToCommit: return "readyToCommit";
    case OtaUpdateState::Failed: return "failed";
    case OtaUpdateState::RebootPending: return "rebootPending";
  }
  return "unknown";
}

bool parseHexNibble(char c, uint8_t &value) {
  if (c >= '0' && c <= '9') {
    value = static_cast<uint8_t>(c - '0');
    return true;
  }
  if (c >= 'a' && c <= 'f') {
    value = static_cast<uint8_t>(c - 'a' + 10);
    return true;
  }
  if (c >= 'A' && c <= 'F') {
    value = static_cast<uint8_t>(c - 'A' + 10);
    return true;
  }
  return false;
}

bool parseSha256(const String &text, uint8_t out[32]) {
  if (text.length() != 64) return false;
  for (size_t i = 0; i < 32; ++i) {
    uint8_t high = 0;
    uint8_t low = 0;
    if (!parseHexNibble(text[i * 2], high) ||
        !parseHexNibble(text[i * 2 + 1], low)) {
      return false;
    }
    out[i] = static_cast<uint8_t>((high << 4) | low);
  }
  return true;
}

void shaInit(Sha256State &sha) {
  mbedtls_sha256_init(&sha.context);
  mbedtls_sha256_starts_ret(&sha.context, 0);
  sha.initialized = true;
}

void shaFree(Sha256State &sha) {
  if (!sha.initialized) return;
  mbedtls_sha256_free(&sha.context);
  sha.initialized = false;
}

bool shaFinishAndVerify(Sha256State &sha) {
  if (!sha.initialized) return false;
  uint8_t actual[32] = {};
  bool ok = mbedtls_sha256_finish_ret(&sha.context, actual) == 0 &&
            memcmp(actual, sha.expected, sizeof(actual)) == 0;
  shaFree(sha);
  return ok;
}

void abortOpenAppHandle() {
  if (!g_session.appHandleOpen) return;
  esp_ota_abort(g_session.appHandle);
  g_session.appHandleOpen = false;
  g_session.appHandle = 0;
}

void resetSession(bool requestResume) {
  abortOpenAppHandle();
  shaFree(g_session.firmwareSha);
  shaFree(g_session.fsSha);
  bool wasPaused = g_session.telemetryPaused;
  g_session = UpdateSession{};
  if (requestResume && wasPaused) {
    g_session.resumeRequested = true;
  }
}

bool fail(const String &message, String &error) {
  abortOpenAppHandle();
  shaFree(g_session.firmwareSha);
  shaFree(g_session.fsSha);
  g_session.state = OtaUpdateState::Failed;
  g_session.error = message;
  error = message;
  if (g_session.telemetryPaused) {
    g_session.resumeRequested = true;
  }
  Serial.printf("[OTA] %s\n", message.c_str());
  return false;
}

bool readSize(const JsonDocument &request,
              const char *key,
              size_t &value,
              String &error) {
  if (!request[key].is<unsigned long>()) {
    error = String("Missing or invalid ") + key;
    return false;
  }
  value = request[key].as<unsigned long>();
  if (value == 0) {
    error = String(key) + " must be greater than zero";
    return false;
  }
  return true;
}

bool readHash(const JsonDocument &request,
              const char *key,
              uint8_t out[32],
              String &error) {
  const char *hash = request[key] | "";
  if (!parseSha256(hash, out)) {
    error = String("Missing or invalid ") + key;
    return false;
  }
  return true;
}

bool validateCandidateFilesystem(const char *label) {
  fs::LittleFSFS candidate;
  if (!candidate.begin(false, "/fscheck", 5, label)) {
    return false;
  }
  bool valid = otaStateValidateFilesystem(candidate);
  candidate.end();
  return valid;
}

const esp_partition_t *findFsPartition(OtaFsSlot slot) {
  return esp_partition_find_first(
      ESP_PARTITION_TYPE_DATA,
      ESP_PARTITION_SUBTYPE_DATA_SPIFFS,
      otaFsPartitionLabel(slot));
}

}  // namespace

bool otaUpdateStart(const JsonDocument &request, String &error) {
  if (otaUpdateBusy()) {
    error = "Another OTA session is active";
    return false;
  }
  if (otaStateCandidateBoot()) {
    error = "Current release is still waiting for boot confirmation";
    return false;
  }
  resetSession(false);

  const char *mode = request["mode"] | "";
  if (strcmp(mode, "firmware") == 0) {
    g_session.mode = UpdateMode::Firmware;
  } else if (strcmp(mode, "bundle") == 0) {
    g_session.mode = UpdateMode::Bundle;
  } else {
    error = "Mode must be firmware or bundle";
    return false;
  }

  if (!readSize(request, "firmwareSize",
                g_session.expectedFirmwareSize, error) ||
      !readHash(request, "firmwareSha256",
                g_session.firmwareSha.expected, error)) {
    resetSession(false);
    return false;
  }

  g_session.appPartition = esp_ota_get_next_update_partition(nullptr);
  if (!g_session.appPartition ||
      g_session.expectedFirmwareSize > g_session.appPartition->size) {
    error = "Firmware does not fit inactive app partition";
    resetSession(false);
    return false;
  }

  if (g_session.mode == UpdateMode::Bundle) {
    if (!readSize(request, "filesystemSize",
                  g_session.expectedFsSize, error) ||
        !readHash(request, "filesystemSha256",
                  g_session.fsSha.expected, error)) {
      resetSession(false);
      return false;
    }
    g_session.fsPartition = findFsPartition(otaInactiveFsSlot());
    if (!g_session.fsPartition ||
        g_session.expectedFsSize != g_session.fsPartition->size) {
      error = "LittleFS image size must match inactive FS partition";
      resetSession(false);
      return false;
    }
  }

  g_session.state = OtaUpdateState::Prepared;
  g_session.pauseRequested = true;
  g_session.lastActivityMs = millis();
  Serial.printf("[OTA] session prepared: mode=%s app=%s fs=%s\n",
                mode,
                g_session.appPartition->label,
                g_session.fsPartition ? g_session.fsPartition->label : "-");
  return true;
}

bool otaUpdateBeginUpload(const String &type, size_t total, String &error) {
  if (!g_session.telemetryPaused) {
    error = "OTA is waiting for telemetry to stop";
    return false;
  }

  if (type == "filesystem") {
    if (g_session.mode != UpdateMode::Bundle ||
        g_session.state != OtaUpdateState::Prepared ||
        !g_session.fsPartition ||
        total != g_session.expectedFsSize) {
      return fail("Unexpected filesystem upload", error);
    }
    esp_err_t result = esp_partition_erase_range(
        g_session.fsPartition, 0, g_session.fsPartition->size);
    if (result != ESP_OK) {
      return fail(String("Cannot erase inactive FS: ") +
                  esp_err_to_name(result), error);
    }
    shaInit(g_session.fsSha);
    g_session.receivedFs = 0;
    g_session.state = OtaUpdateState::FsReceiving;
    g_session.lastActivityMs = millis();
    return true;
  }

  if (type == "firmware") {
    bool stateAllowed =
        (g_session.mode == UpdateMode::Firmware &&
         g_session.state == OtaUpdateState::Prepared) ||
        (g_session.mode == UpdateMode::Bundle &&
         g_session.state == OtaUpdateState::FsReady);
    if (!stateAllowed || !g_session.appPartition ||
        total != g_session.expectedFirmwareSize) {
      return fail("Unexpected firmware upload", error);
    }
    esp_err_t result = esp_ota_begin(
        g_session.appPartition,
        g_session.expectedFirmwareSize,
        &g_session.appHandle);
    if (result != ESP_OK) {
      return fail(String("Cannot begin firmware update: ") +
                  esp_err_to_name(result), error);
    }
    g_session.appHandleOpen = true;
    shaInit(g_session.firmwareSha);
    g_session.receivedFirmware = 0;
    g_session.state = OtaUpdateState::FirmwareReceiving;
    g_session.lastActivityMs = millis();
    return true;
  }

  error = "Unknown upload type";
  return false;
}

bool otaUpdateWrite(const String &type,
                    size_t index,
                    const uint8_t *data,
                    size_t len,
                    bool final,
                    String &error) {
  if (type == "filesystem") {
    if (g_session.state != OtaUpdateState::FsReceiving ||
        index != g_session.receivedFs ||
        index + len > g_session.expectedFsSize) {
      return fail("Invalid filesystem upload offset", error);
    }
    esp_err_t result = esp_partition_write(
        g_session.fsPartition, index, data, len);
    if (result != ESP_OK) {
      return fail(String("Filesystem write failed: ") +
                  esp_err_to_name(result), error);
    }
    mbedtls_sha256_update_ret(&g_session.fsSha.context, data, len);
    g_session.receivedFs += len;
    g_session.lastActivityMs = millis();
    if (final) {
      if (g_session.receivedFs != g_session.expectedFsSize ||
          !shaFinishAndVerify(g_session.fsSha)) {
        return fail("Filesystem SHA-256 mismatch", error);
      }
      if (!validateCandidateFilesystem(g_session.fsPartition->label)) {
        return fail("Candidate LittleFS validation failed", error);
      }
      g_session.state = OtaUpdateState::FsReady;
    }
    return true;
  }

  if (type == "firmware") {
    if (g_session.state != OtaUpdateState::FirmwareReceiving ||
        index != g_session.receivedFirmware ||
        index + len > g_session.expectedFirmwareSize) {
      return fail("Invalid firmware upload offset", error);
    }
    esp_err_t result = esp_ota_write(g_session.appHandle, data, len);
    if (result != ESP_OK) {
      return fail(String("Firmware write failed: ") +
                  esp_err_to_name(result), error);
    }
    mbedtls_sha256_update_ret(&g_session.firmwareSha.context, data, len);
    g_session.receivedFirmware += len;
    g_session.lastActivityMs = millis();
    if (final) {
      if (g_session.receivedFirmware != g_session.expectedFirmwareSize ||
          !shaFinishAndVerify(g_session.firmwareSha)) {
        return fail("Firmware SHA-256 mismatch", error);
      }
      result = esp_ota_end(g_session.appHandle);
      g_session.appHandleOpen = false;
      g_session.appHandle = 0;
      if (result != ESP_OK) {
        return fail(String("Firmware image validation failed: ") +
                    esp_err_to_name(result), error);
      }
      esp_app_desc_t description{};
      result = esp_ota_get_partition_description(
          g_session.appPartition, &description);
      if (result != ESP_OK) {
        return fail("Cannot read firmware description", error);
      }
      g_session.state = OtaUpdateState::ReadyToCommit;
      Serial.printf("[OTA] firmware ready: project=%s version=%s\n",
                    description.project_name, description.version);
    }
    return true;
  }

  error = "Unknown upload type";
  return false;
}

bool otaUpdateCommit(String &error) {
  if (g_session.state != OtaUpdateState::ReadyToCommit ||
      !g_session.appPartition) {
    error = "OTA session is not ready to commit";
    return false;
  }

  uint8_t candidateApp = otaAppIndex(g_session.appPartition);
  OtaFsSlot candidateFs = g_session.mode == UpdateMode::Bundle
                            ? otaInactiveFsSlot()
                            : otaActiveFsSlot();
  bool candidateHasFs = g_session.mode == UpdateMode::Bundle;
  if (!otaStateStageUpdate(candidateApp, candidateFs, candidateHasFs)) {
    return fail("Cannot persist OTA rollback metadata", error);
  }

  esp_err_t result = esp_ota_set_boot_partition(g_session.appPartition);
  if (result != ESP_OK) {
    otaStateCancelUpdate();
    return fail(String("Cannot select candidate app: ") +
                esp_err_to_name(result), error);
  }

  g_session.state = OtaUpdateState::RebootPending;
  g_session.rebootAtMs = millis() + 1000;
  return true;
}

void otaUpdateCancel() {
  if (g_session.state == OtaUpdateState::RebootPending) return;
  if (g_session.state != OtaUpdateState::Idle) {
    otaStateCancelUpdate();
  }
  resetSession(true);
}

bool otaUpdateManualRollback(String &error) {
  if (otaUpdateBusy()) {
    error = "Cannot rollback while OTA session is active";
    return false;
  }

  uint8_t targetApp = UINT8_MAX;
  OtaFsSlot targetFs = OtaFsSlot::Fs0;
  if (!otaStateStageManualRollback(targetApp, targetFs)) {
    error = "Previous release is not available";
    return false;
  }

  const esp_partition_t *partition = otaAppPartition(targetApp);
  if (!partition) {
    otaStateCancelUpdate();
    error = "Previous app partition is missing";
    return false;
  }
  esp_app_desc_t description{};
  if (esp_ota_get_partition_description(partition, &description) != ESP_OK) {
    otaStateCancelUpdate();
    error = "Previous app image is invalid";
    return false;
  }
  esp_err_t result = esp_ota_set_boot_partition(partition);
  if (result != ESP_OK) {
    otaStateCancelUpdate();
    error = String("Cannot select previous app: ") +
            esp_err_to_name(result);
    return false;
  }

  g_session.state = OtaUpdateState::RebootPending;
  g_session.rebootAtMs = millis() + 1000;
  return true;
}

void otaUpdateLoop() {
  uint32_t nowMs = millis();
  if (g_session.state != OtaUpdateState::Idle &&
      g_session.state != OtaUpdateState::Failed &&
      g_session.state != OtaUpdateState::RebootPending &&
      nowMs - g_session.lastActivityMs >= OTA_SESSION_TIMEOUT_MS) {
    String ignored;
    fail("OTA session timed out", ignored);
  }
  if (g_session.state == OtaUpdateState::RebootPending &&
      static_cast<int32_t>(nowMs - g_session.rebootAtMs) >= 0) {
    ESP.restart();
  }
}

void otaUpdateAddStatus(JsonDocument &doc) {
  doc["state"] = stateName(g_session.state);
  doc["phase"] = stateName(g_session.state);
  doc["ready"] = g_session.telemetryPaused;
  doc["mode"] =
      g_session.mode == UpdateMode::Bundle ? "bundle" : "firmware";
  doc["received"] =
      g_session.state == OtaUpdateState::FsReceiving
          ? g_session.receivedFs
          : g_session.receivedFirmware;
  doc["total"] =
      g_session.state == OtaUpdateState::FsReceiving
          ? g_session.expectedFsSize
          : g_session.expectedFirmwareSize;
  doc["error"] = g_session.error;
  doc["rollbackAvailable"] = otaStateManualRollbackAvailable();
  doc["activeFs"] = otaFsPartitionLabel(otaActiveFsSlot());
  doc["runningApp"] = otaRunningAppIndex();
}

OtaUpdateState otaUpdateState() {
  return g_session.state;
}

const String &otaUpdateLastError() {
  return g_session.error;
}

bool otaUpdateBusy() {
  return g_session.state != OtaUpdateState::Idle;
}

bool otaUpdateTelemetryPauseRequested() {
  return g_session.pauseRequested && !g_session.telemetryPaused;
}

void otaUpdateSetTelemetryPaused(bool paused) {
  g_session.pauseRequested = false;
  if (!paused) {
    String ignored;
    fail("Telemetry task did not stop", ignored);
    return;
  }
  g_session.telemetryPaused = true;
}

bool otaUpdateTelemetryResumeRequested() {
  return g_session.resumeRequested;
}

void otaUpdateSetTelemetryResumed() {
  g_session.resumeRequested = false;
  g_session.telemetryPaused = false;
  if (g_session.state == OtaUpdateState::Failed) {
    String lastError = g_session.error;
    resetSession(false);
    g_session.error = lastError;
  }
}
