#include "power_manager.h"

#include "display/ui.h"
#include "net/time_service.h"

namespace {

PowerSaveSettings g_settings;
uint32_t g_lastActivityMs = 0;
uint32_t g_lastTickMs = 0;
uint32_t g_wakeUntilMs = 0;
bool g_wakeOverride = false;
bool g_manualOverride = false;
BrightnessLevel g_manualBrightness = BrightnessLevel::Full;
bool g_haveNightState = false;
bool g_lastNightState = false;

bool deadlineReached(uint32_t nowMs, uint32_t deadlineMs) {
  return static_cast<int32_t>(nowMs - deadlineMs) >= 0;
}

bool currentNightState(bool &timeValid, uint16_t &localMinute) {
  timeValid = timeServiceLocalMinute(localMinute);
  return timeValid && g_settings.scheduleEnabled &&
         powerPolicyNightPeriod(localMinute,
                                g_settings.nightStartMinute,
                                g_settings.nightEndMinute);
}

void applyPolicy(uint32_t nowMs) {
  bool timeValid = false;
  uint16_t localMinute = 0;
  bool night = currentNightState(timeValid, localMinute);

  if (timeValid) {
    if (g_haveNightState && night != g_lastNightState) {
      g_manualOverride = false;
      g_wakeOverride = false;
      Serial.printf("[Power] schedule period -> %s\n",
                    night ? "night" : "day");
    }
    g_haveNightState = true;
    g_lastNightState = night;
  } else {
    g_haveNightState = false;
  }

  if (g_wakeOverride && deadlineReached(nowMs, g_wakeUntilMs)) {
    g_wakeOverride = false;
  }

  const uint32_t idleMs = nowMs - g_lastActivityMs;
  const uint64_t timeoutMs =
      static_cast<uint64_t>(g_settings.autoOffMinutes) * 60ULL * 1000ULL;
  const bool idleExpired =
      g_settings.autoOffEnabled &&
      static_cast<uint64_t>(idleMs) >= timeoutMs;

  PowerPolicyInput input;
  input.startupBrightness = g_settings.startupBrightness;
  input.nightBrightness = g_settings.nightBrightness;
  input.autoOffEnabled = g_settings.autoOffEnabled;
  input.autoOffMinutes = g_settings.autoOffMinutes;
  input.scheduleEnabled = g_settings.scheduleEnabled;
  input.nightStartMinute = g_settings.nightStartMinute;
  input.nightEndMinute = g_settings.nightEndMinute;
  input.timeValid = timeValid;
  input.localMinute = localMinute;
  input.idleMs = idleMs;
  input.wakeOverride = g_wakeOverride;

  BrightnessLevel target = powerPolicyBrightness(input);
  if (g_manualOverride && !idleExpired && !g_wakeOverride) {
    target = g_manualBrightness;
  }
  if (uiBrightness() != target) {
    uiSetBrightness(target);
  }
}

BrightnessLevel nextManualBrightness(BrightnessLevel current) {
  switch (current) {
    case BrightnessLevel::Full:
      return BrightnessLevel::ThreeQuarters;
    case BrightnessLevel::ThreeQuarters:
      return BrightnessLevel::Half;
    case BrightnessLevel::Half:
      return BrightnessLevel::Quarter;
    case BrightnessLevel::Quarter:
      return BrightnessLevel::Off;
    case BrightnessLevel::Off:
    default:
      return BrightnessLevel::Full;
  }
}

}  // namespace

void powerManagerBegin(const PowerSaveSettings &settings, uint32_t nowMs) {
  g_settings = settings;
  g_lastActivityMs = nowMs;
  g_lastTickMs = nowMs;
  g_wakeOverride = false;
  g_manualOverride = false;
  g_haveNightState = false;
  applyPolicy(nowMs);
}

void powerManagerApplySettings(const PowerSaveSettings &settings,
                               uint32_t nowMs) {
  g_settings = settings;
  g_lastActivityMs = nowMs;
  g_wakeOverride = false;
  g_manualOverride = false;
  g_haveNightState = false;
  timeServiceSetZone(settings.timeZone);
  applyPolicy(nowMs);
}

void powerManagerTick(uint32_t nowMs) {
  if (nowMs - g_lastTickMs < 1000) return;
  g_lastTickMs = nowMs;
  applyPolicy(nowMs);
}

bool powerManagerHandleActivity(uint32_t nowMs) {
  const bool wasOff = !uiDisplayEnabled();
  g_lastActivityMs = nowMs;

  if (wasOff) {
    g_manualOverride = false;
    g_wakeOverride = true;
    const uint32_t wakeDurationMs =
        g_settings.autoOffEnabled
            ? static_cast<uint32_t>(g_settings.autoOffMinutes) * 60UL * 1000UL
            : 60UL * 1000UL;
    g_wakeUntilMs = nowMs + wakeDurationMs;
  } else if (g_wakeOverride) {
    const uint32_t wakeDurationMs =
        g_settings.autoOffEnabled
            ? static_cast<uint32_t>(g_settings.autoOffMinutes) * 60UL * 1000UL
            : 60UL * 1000UL;
    g_wakeUntilMs = nowMs + wakeDurationMs;
  }

  applyPolicy(nowMs);
  return wasOff;
}

void powerManagerCycleManualBrightness(uint32_t nowMs) {
  g_lastActivityMs = nowMs;
  g_wakeOverride = false;
  g_manualOverride = true;
  g_manualBrightness = nextManualBrightness(uiBrightness());
  applyPolicy(nowMs);
}
