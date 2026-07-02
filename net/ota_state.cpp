#include "ota_state.h"

#include <Preferences.h>
#include <esp_ota_ops.h>

namespace {

constexpr char NVS_NAMESPACE[] = "netmon_ota";
constexpr uint8_t OTA_STATE_SCHEMA_VERSION = 1;
constexpr uint32_t BOOT_CONFIRM_DELAY_MS = 10000;

OtaBootMetadata g_metadata{};
OtaBootDecision g_decision{};
bool g_prepared = false;
bool g_confirmed = false;

uint8_t appIndex(const esp_partition_t *partition) {
  if (!partition ||
      partition->subtype < ESP_PARTITION_SUBTYPE_APP_OTA_0 ||
      partition->subtype > ESP_PARTITION_SUBTYPE_APP_OTA_15) {
    return UINT8_MAX;
  }
  return static_cast<uint8_t>(
      partition->subtype - ESP_PARTITION_SUBTYPE_APP_OTA_0);
}

bool loadMetadata(OtaBootMetadata &metadata) {
  Preferences prefs;
  if (!prefs.begin(NVS_NAMESPACE, true)) {
    return false;
  }

  uint8_t version = prefs.getUChar("ver", 0);
  if (version != OTA_STATE_SCHEMA_VERSION) {
    prefs.end();
    return false;
  }

  metadata.valid = true;
  metadata.activeFs =
      static_cast<OtaFsSlot>(prefs.getUChar("activeFs", 0));
  metadata.previousFs =
      static_cast<OtaFsSlot>(prefs.getUChar("prevFs", 0));
  metadata.candidateFs =
      static_cast<OtaFsSlot>(prefs.getUChar("candFs", 0));
  metadata.previousApp = prefs.getUChar("prevApp", UINT8_MAX);
  metadata.candidateApp = prefs.getUChar("candApp", UINT8_MAX);
  metadata.pending = prefs.getBool("pending", false);
  metadata.candidateHasFs = prefs.getBool("candHasFs", false);
  metadata.manualRollbackAvailable = prefs.getBool("rollback", false);
  prefs.end();
  return true;
}

bool saveMetadata(const OtaBootMetadata &metadata) {
  Preferences prefs;
  if (!prefs.begin(NVS_NAMESPACE, false)) {
    return false;
  }

  bool ok = true;
  ok = ok && prefs.putUChar("ver", OTA_STATE_SCHEMA_VERSION) == 1;
  ok = ok && prefs.putUChar("activeFs",
                            static_cast<uint8_t>(metadata.activeFs)) == 1;
  ok = ok && prefs.putUChar("prevFs",
                            static_cast<uint8_t>(metadata.previousFs)) == 1;
  ok = ok && prefs.putUChar("candFs",
                            static_cast<uint8_t>(metadata.candidateFs)) == 1;
  ok = ok && prefs.putUChar("prevApp", metadata.previousApp) == 1;
  ok = ok && prefs.putUChar("candApp", metadata.candidateApp) == 1;
  ok = ok && prefs.putBool("pending", metadata.pending) == 1;
  ok = ok && prefs.putBool("candHasFs", metadata.candidateHasFs) == 1;
  ok = ok && prefs.putBool("rollback",
                           metadata.manualRollbackAvailable) == 1;
  prefs.end();
  return ok;
}

void initializeMetadata() {
  g_metadata = OtaBootMetadata{};
  g_metadata.valid = true;
  g_metadata.activeFs = OtaFsSlot::Fs0;
  g_metadata.previousFs = OtaFsSlot::Fs0;
  g_metadata.candidateFs = OtaFsSlot::Fs0;
  saveMetadata(g_metadata);
}

bool runningAppPendingVerify() {
  const esp_partition_t *running = esp_ota_get_running_partition();
  if (!running) return false;

  esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
  return esp_ota_get_state_partition(running, &state) == ESP_OK &&
         state == ESP_OTA_IMG_PENDING_VERIFY;
}

}  // namespace

const char *otaFsPartitionLabel(OtaFsSlot slot) {
  return slot == OtaFsSlot::Fs1 ? "fs1" : "fs0";
}

uint8_t otaRunningAppIndex() {
  return appIndex(esp_ota_get_running_partition());
}

uint8_t otaAppIndex(const esp_partition_t *partition) {
  return appIndex(partition);
}

const esp_partition_t *otaAppPartition(uint8_t index) {
  if (index > 15) return nullptr;
  return esp_partition_find_first(
      ESP_PARTITION_TYPE_APP,
      static_cast<esp_partition_subtype_t>(
          ESP_PARTITION_SUBTYPE_APP_OTA_0 + index),
      nullptr);
}

OtaFsSlot otaActiveFsSlot() {
  if (!g_prepared) {
    OtaFsSlot ignored;
    otaStatePrepareBoot(ignored);
  }
  return g_decision.fs;
}

OtaFsSlot otaInactiveFsSlot() {
  return otaOtherFsSlot(otaActiveFsSlot());
}

bool otaStatePrepareBoot(OtaFsSlot &slot) {
  if (!loadMetadata(g_metadata)) {
    initializeMetadata();
  }

  g_decision = otaDecideBoot(g_metadata, otaRunningAppIndex());
  if (g_decision.metadataInvalid) {
    Serial.println("[OTA] invalid metadata, using fs0");
    initializeMetadata();
    g_decision = otaDecideBoot(g_metadata, otaRunningAppIndex());
  } else if (g_decision.rollbackDetected) {
    Serial.println("[OTA] bootloader rollback detected, restoring previous FS");
    g_metadata.activeFs = g_metadata.previousFs;
    g_metadata.pending = false;
    g_metadata.candidateHasFs = false;
    g_metadata.manualRollbackAvailable = false;
    saveMetadata(g_metadata);
    g_decision = otaDecideBoot(g_metadata, otaRunningAppIndex());
  } else if (g_decision.candidateBoot && !runningAppPendingVerify()) {
    // Ручной откат выбирает уже подтверждённый app-раздел. Для него загрузчик
    // не создаёт PENDING_VERIFY, поэтому подтверждаем связанную FS здесь.
    g_metadata.activeFs = g_decision.fs;
    g_metadata.pending = false;
    g_metadata.candidateHasFs = false;
    g_metadata.manualRollbackAvailable =
        g_metadata.previousApp != UINT8_MAX;
    saveMetadata(g_metadata);
    g_decision = otaDecideBoot(g_metadata, otaRunningAppIndex());
  }

  slot = g_decision.fs;
  g_prepared = true;
  Serial.printf("[OTA] running app=%u, FS=%s, candidate=%s\n",
                static_cast<unsigned>(otaRunningAppIndex()),
                otaFsPartitionLabel(slot),
                g_decision.candidateBoot ? "yes" : "no");
  return true;
}

bool otaStateValidateFilesystem(fs::FS &filesystem) {
  static constexpr const char *REQUIRED_FILES[] = {
      "/portal/index.html",
      "/ota/index.html",
      "/ota/ota.html",
  };

  for (const char *path : REQUIRED_FILES) {
    File file = filesystem.open(path, "r");
    if (!file || file.isDirectory() || file.size() == 0) {
      Serial.printf("[OTA] required FS file is missing: %s\n", path);
      if (file) file.close();
      return false;
    }
    file.close();
  }
  return true;
}

bool otaStateCandidateBoot() {
  return g_prepared && g_decision.candidateBoot;
}

void otaStateHandleFilesystemFailure() {
  if (!otaStateCandidateBoot() || !runningAppPendingVerify()) {
    Serial.println("[OTA] active filesystem is invalid; rollback unavailable");
    return;
  }

  Serial.println("[OTA] candidate filesystem invalid, rolling back");
  esp_err_t error = esp_ota_mark_app_invalid_rollback_and_reboot();
  Serial.printf("[OTA] rollback failed: %s\n", esp_err_to_name(error));
}

void otaStateTick(uint32_t nowMs, bool filesystemReady) {
  if (g_confirmed || !filesystemReady || nowMs < BOOT_CONFIRM_DELAY_MS ||
      !runningAppPendingVerify()) {
    return;
  }

  esp_err_t error = esp_ota_mark_app_valid_cancel_rollback();
  if (error != ESP_OK) {
    Serial.printf("[OTA] failed to confirm app: %s\n", esp_err_to_name(error));
    return;
  }

  if (g_metadata.pending &&
      otaRunningAppIndex() == g_metadata.candidateApp) {
    g_metadata.activeFs = g_decision.fs;
    g_metadata.pending = false;
    g_metadata.candidateHasFs = false;
    g_metadata.manualRollbackAvailable =
        g_metadata.previousApp != UINT8_MAX;
    if (!saveMetadata(g_metadata)) {
      Serial.println("[OTA] failed to persist confirmed release metadata");
    }
    g_decision = otaDecideBoot(g_metadata, otaRunningAppIndex());
  }

  g_confirmed = true;
  Serial.println("[OTA] application and filesystem confirmed");
}

bool otaStateStageUpdate(uint8_t candidateApp,
                         OtaFsSlot candidateFs,
                         bool candidateHasFs) {
  if (!g_prepared) {
    OtaFsSlot ignored;
    otaStatePrepareBoot(ignored);
  }
  if (candidateApp == UINT8_MAX || !otaFsSlotValid(candidateFs)) {
    return false;
  }

  g_metadata.previousApp = otaRunningAppIndex();
  g_metadata.previousFs = g_decision.fs;
  g_metadata.candidateApp = candidateApp;
  g_metadata.candidateFs = candidateFs;
  g_metadata.candidateHasFs = candidateHasFs;
  g_metadata.pending = true;
  g_metadata.manualRollbackAvailable = false;
  return saveMetadata(g_metadata);
}

void otaStateCancelUpdate() {
  if (!g_prepared) return;
  g_metadata.pending = false;
  g_metadata.candidateHasFs = false;
  saveMetadata(g_metadata);
}

bool otaStateManualRollbackAvailable() {
  if (!g_prepared) {
    OtaFsSlot ignored;
    otaStatePrepareBoot(ignored);
  }
  return g_metadata.manualRollbackAvailable &&
         g_metadata.previousApp != UINT8_MAX &&
         otaFsSlotValid(g_metadata.previousFs);
}

bool otaStateStageManualRollback(uint8_t &targetApp, OtaFsSlot &targetFs) {
  if (!otaStateManualRollbackAvailable()) return false;

  targetApp = g_metadata.previousApp;
  targetFs = g_metadata.previousFs;

  uint8_t currentApp = otaRunningAppIndex();
  OtaFsSlot currentFs = g_metadata.activeFs;
  g_metadata.previousApp = currentApp;
  g_metadata.previousFs = currentFs;
  g_metadata.candidateApp = targetApp;
  g_metadata.candidateFs = targetFs;
  g_metadata.candidateHasFs = true;
  g_metadata.pending = true;
  g_metadata.manualRollbackAvailable = false;
  return saveMetadata(g_metadata);
}
