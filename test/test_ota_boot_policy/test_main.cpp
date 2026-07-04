#include <unity.h>

#include "../../net/ota_boot_policy.h"

static OtaBootMetadata validMetadata() {
  OtaBootMetadata metadata{};
  metadata.valid = true;
  metadata.activeFs = OtaFsSlot::Fs0;
  metadata.previousFs = OtaFsSlot::Fs0;
  metadata.candidateFs = OtaFsSlot::Fs1;
  metadata.previousApp = 0;
  metadata.candidateApp = 1;
  return metadata;
}

static void test_normal_boot_uses_active_fs() {
  OtaBootMetadata metadata = validMetadata();
  OtaBootDecision decision = otaDecideBoot(metadata, 0);

  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(OtaFsSlot::Fs0),
                          static_cast<uint8_t>(decision.fs));
  TEST_ASSERT_FALSE(decision.candidateBoot);
  TEST_ASSERT_FALSE(decision.rollbackDetected);
}

static void test_bundle_candidate_uses_candidate_fs() {
  OtaBootMetadata metadata = validMetadata();
  metadata.pending = true;
  metadata.candidateHasFs = true;

  OtaBootDecision decision = otaDecideBoot(metadata, 1);

  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(OtaFsSlot::Fs1),
                          static_cast<uint8_t>(decision.fs));
  TEST_ASSERT_TRUE(decision.candidateBoot);
  TEST_ASSERT_FALSE(decision.rollbackDetected);
}

static void test_firmware_only_candidate_keeps_active_fs() {
  OtaBootMetadata metadata = validMetadata();
  metadata.pending = true;
  metadata.candidateHasFs = false;

  OtaBootDecision decision = otaDecideBoot(metadata, 1);

  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(OtaFsSlot::Fs0),
                          static_cast<uint8_t>(decision.fs));
  TEST_ASSERT_TRUE(decision.candidateBoot);
}

static void test_rollback_restores_previous_fs() {
  OtaBootMetadata metadata = validMetadata();
  metadata.activeFs = OtaFsSlot::Fs1;
  metadata.previousFs = OtaFsSlot::Fs0;
  metadata.pending = true;
  metadata.candidateHasFs = true;

  OtaBootDecision decision = otaDecideBoot(metadata, 0);

  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(OtaFsSlot::Fs0),
                          static_cast<uint8_t>(decision.fs));
  TEST_ASSERT_FALSE(decision.candidateBoot);
  TEST_ASSERT_TRUE(decision.rollbackDetected);
}

static void test_manual_rollback_uses_previous_pair_as_candidate() {
  OtaBootMetadata metadata = validMetadata();
  metadata.activeFs = OtaFsSlot::Fs1;
  metadata.previousFs = OtaFsSlot::Fs0;
  metadata.pending = true;
  metadata.candidateHasFs = true;
  metadata.previousApp = 1;
  metadata.candidateApp = 0;
  metadata.candidateFs = OtaFsSlot::Fs0;

  OtaBootDecision decision = otaDecideBoot(metadata, 0);

  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(OtaFsSlot::Fs0),
                          static_cast<uint8_t>(decision.fs));
  TEST_ASSERT_TRUE(decision.candidateBoot);
  TEST_ASSERT_FALSE(decision.rollbackDetected);
}

static void test_invalid_metadata_falls_back_to_fs0() {
  OtaBootMetadata metadata = validMetadata();
  metadata.valid = false;
  metadata.activeFs = static_cast<OtaFsSlot>(99);

  OtaBootDecision decision = otaDecideBoot(metadata, 0);

  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(OtaFsSlot::Fs0),
                          static_cast<uint8_t>(decision.fs));
  TEST_ASSERT_TRUE(decision.metadataInvalid);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_normal_boot_uses_active_fs);
  RUN_TEST(test_bundle_candidate_uses_candidate_fs);
  RUN_TEST(test_firmware_only_candidate_keeps_active_fs);
  RUN_TEST(test_rollback_restores_previous_fs);
  RUN_TEST(test_manual_rollback_uses_previous_pair_as_candidate);
  RUN_TEST(test_invalid_metadata_falls_back_to_fs0);
  return UNITY_END();
}
