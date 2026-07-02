#include <unity.h>

#include "../../display/power_policy.h"

static PowerPolicyInput defaults() {
  PowerPolicyInput input{};
  input.startupBrightness = BrightnessLevel::Full;
  input.nightBrightness = BrightnessLevel::Quarter;
  input.autoOffMinutes = 5;
  input.nightStartMinute = 23 * 60;
  input.nightEndMinute = 7 * 60;
  input.timeValid = true;
  return input;
}

static void assertBrightness(BrightnessLevel expected,
                             const PowerPolicyInput &input) {
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(expected),
                          static_cast<uint8_t>(
                              powerPolicyBrightness(input)));
}

static void test_day_uses_startup_brightness() {
  PowerPolicyInput input = defaults();
  input.scheduleEnabled = true;
  input.localMinute = 12 * 60;
  assertBrightness(BrightnessLevel::Full, input);
}

static void test_night_period_crosses_midnight() {
  PowerPolicyInput input = defaults();
  input.scheduleEnabled = true;

  input.localMinute = 23 * 60;
  assertBrightness(BrightnessLevel::Quarter, input);
  input.localMinute = 2 * 60;
  assertBrightness(BrightnessLevel::Quarter, input);
  input.localMinute = 7 * 60;
  assertBrightness(BrightnessLevel::Full, input);
}

static void test_regular_period_does_not_cross_midnight() {
  PowerPolicyInput input = defaults();
  input.scheduleEnabled = true;
  input.nightStartMinute = 10 * 60;
  input.nightEndMinute = 12 * 60;

  input.localMinute = 11 * 60;
  assertBrightness(BrightnessLevel::Quarter, input);
  input.localMinute = 13 * 60;
  assertBrightness(BrightnessLevel::Full, input);
}

static void test_equal_start_and_end_disables_night_period() {
  PowerPolicyInput input = defaults();
  input.scheduleEnabled = true;
  input.nightStartMinute = 60;
  input.nightEndMinute = 60;
  input.localMinute = 60;
  assertBrightness(BrightnessLevel::Full, input);
}

static void test_idle_timeout_turns_display_off() {
  PowerPolicyInput input = defaults();
  input.autoOffEnabled = true;
  input.idleMs = 5UL * 60UL * 1000UL;
  assertBrightness(BrightnessLevel::Off, input);
}

static void test_wake_override_beats_idle_and_night_off() {
  PowerPolicyInput input = defaults();
  input.scheduleEnabled = true;
  input.nightBrightness = BrightnessLevel::Off;
  input.localMinute = 2 * 60;
  input.autoOffEnabled = true;
  input.idleMs = 30UL * 60UL * 1000UL;
  input.wakeOverride = true;
  assertBrightness(BrightnessLevel::Full, input);
}

static void test_missing_time_uses_startup_brightness() {
  PowerPolicyInput input = defaults();
  input.scheduleEnabled = true;
  input.timeValid = false;
  input.localMinute = 2 * 60;
  assertBrightness(BrightnessLevel::Full, input);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_day_uses_startup_brightness);
  RUN_TEST(test_night_period_crosses_midnight);
  RUN_TEST(test_regular_period_does_not_cross_midnight);
  RUN_TEST(test_equal_start_and_end_disables_night_period);
  RUN_TEST(test_idle_timeout_turns_display_off);
  RUN_TEST(test_wake_override_beats_idle_and_night_off);
  RUN_TEST(test_missing_time_uses_startup_brightness);
  return UNITY_END();
}
