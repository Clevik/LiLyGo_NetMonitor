#pragma once

#include <stdint.h>

enum class BrightnessLevel : uint8_t {
  Off = 0,
  Quarter = 25,
  Half = 50,
  ThreeQuarters = 75,
  Full = 100,
};

inline bool isBrightnessLevelSupported(BrightnessLevel level,
                                       bool allowOff) {
  return (allowOff && level == BrightnessLevel::Off) ||
         level == BrightnessLevel::Quarter ||
         level == BrightnessLevel::Half ||
         level == BrightnessLevel::ThreeQuarters ||
         level == BrightnessLevel::Full;
}

struct PowerPolicyInput {
  BrightnessLevel startupBrightness = BrightnessLevel::Full;
  BrightnessLevel nightBrightness = BrightnessLevel::Quarter;
  bool autoOffEnabled = false;
  uint16_t autoOffMinutes = 5;
  bool scheduleEnabled = false;
  uint16_t nightStartMinute = 23 * 60;
  uint16_t nightEndMinute = 7 * 60;
  bool timeValid = false;
  uint16_t localMinute = 0;
  uint32_t idleMs = 0;
  bool wakeOverride = false;
};

inline bool powerPolicyNightPeriod(uint16_t minute,
                                   uint16_t start,
                                   uint16_t end) {
  if (start == end) return false;
  if (start < end) {
    return minute >= start && minute < end;
  }
  return minute >= start || minute < end;
}

inline BrightnessLevel powerPolicyBrightness(
    const PowerPolicyInput &input) {
  if (input.wakeOverride) {
    return input.startupBrightness;
  }

  uint64_t timeoutMs =
      static_cast<uint64_t>(input.autoOffMinutes) * 60ULL * 1000ULL;
  if (input.autoOffEnabled &&
      static_cast<uint64_t>(input.idleMs) >= timeoutMs) {
    return BrightnessLevel::Off;
  }

  if (input.timeValid && input.scheduleEnabled &&
      powerPolicyNightPeriod(input.localMinute,
                             input.nightStartMinute,
                             input.nightEndMinute)) {
    return input.nightBrightness;
  }

  return input.startupBrightness;
}
