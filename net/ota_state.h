#pragma once

#include <Arduino.h>
#include <FS.h>
#include <esp_partition.h>

#include "ota_boot_policy.h"

const char *otaFsPartitionLabel(OtaFsSlot slot);
uint8_t otaRunningAppIndex();
uint8_t otaAppIndex(const esp_partition_t *partition);
const esp_partition_t *otaAppPartition(uint8_t appIndex);
OtaFsSlot otaActiveFsSlot();
OtaFsSlot otaInactiveFsSlot();

bool otaStatePrepareBoot(OtaFsSlot &slot);
bool otaStateValidateFilesystem(fs::FS &filesystem);
bool otaStateCandidateBoot();
void otaStateHandleFilesystemFailure();
void otaStateTick(uint32_t nowMs, bool filesystemReady);

bool otaStateStageUpdate(uint8_t candidateApp,
                         OtaFsSlot candidateFs,
                         bool candidateHasFs);
void otaStateCancelUpdate();
bool otaStateManualRollbackAvailable();
bool otaStateStageManualRollback(uint8_t &targetApp, OtaFsSlot &targetFs);
