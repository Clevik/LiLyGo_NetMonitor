#pragma once

#include <stdint.h>

enum class OtaFsSlot : uint8_t {
  Fs0 = 0,
  Fs1 = 1,
};

struct OtaBootMetadata {
  bool      valid = false;
  OtaFsSlot activeFs = OtaFsSlot::Fs0;
  OtaFsSlot previousFs = OtaFsSlot::Fs0;
  OtaFsSlot candidateFs = OtaFsSlot::Fs0;
  uint8_t   previousApp = UINT8_MAX;
  uint8_t   candidateApp = UINT8_MAX;
  bool      pending = false;
  bool      candidateHasFs = false;
  bool      manualRollbackAvailable = false;
};

struct OtaBootDecision {
  OtaFsSlot fs = OtaFsSlot::Fs0;
  bool candidateBoot = false;
  bool rollbackDetected = false;
  bool metadataInvalid = false;
};

inline bool otaFsSlotValid(OtaFsSlot slot) {
  return slot == OtaFsSlot::Fs0 || slot == OtaFsSlot::Fs1;
}

inline OtaFsSlot otaOtherFsSlot(OtaFsSlot slot) {
  return slot == OtaFsSlot::Fs0 ? OtaFsSlot::Fs1 : OtaFsSlot::Fs0;
}

inline OtaBootDecision otaDecideBoot(const OtaBootMetadata &metadata,
                                     uint8_t runningApp) {
  OtaBootDecision decision{};
  if (!metadata.valid || !otaFsSlotValid(metadata.activeFs) ||
      !otaFsSlotValid(metadata.previousFs) ||
      !otaFsSlotValid(metadata.candidateFs)) {
    decision.metadataInvalid = true;
    return decision;
  }

  if (!metadata.pending) {
    decision.fs = metadata.activeFs;
    return decision;
  }

  if (runningApp == metadata.candidateApp) {
    decision.candidateBoot = true;
    decision.fs = metadata.candidateHasFs
                    ? metadata.candidateFs
                    : metadata.activeFs;
    return decision;
  }

  decision.fs = metadata.previousFs;
  decision.rollbackDetected = true;
  return decision;
}
