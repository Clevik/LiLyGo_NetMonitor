#include "time_service.h"

#include <AceTime.h>
#include <time.h>

namespace {

using namespace ace_time;

constexpr time_t MIN_VALID_UNIX_TIME = 1704067200;  // 2024-01-01 UTC
constexpr char DEFAULT_TIME_ZONE[] = "Europe/Moscow";

BasicZoneProcessorCache<1> g_zoneProcessorCache;
BasicZoneManager g_zoneManager(
    zonedb2025::kZoneAndLinkRegistrySize,
    zonedb2025::kZoneAndLinkRegistry,
    g_zoneProcessorCache);
BasicZoneProcessorCache<1> g_validationZoneProcessorCache;
BasicZoneManager g_validationZoneManager(
    zonedb2025::kZoneAndLinkRegistrySize,
    zonedb2025::kZoneAndLinkRegistry,
    g_validationZoneProcessorCache);
TimeZone g_timeZone = TimeZone::forError();
String g_timeZoneName = DEFAULT_TIME_ZONE;
bool g_sntpStarted = false;

}  // namespace

void timeServiceBegin(const String &timeZone) {
  if (!g_sntpStarted) {
    configTime(0, 0, "pool.ntp.org", "time.cloudflare.com");
    g_sntpStarted = true;
    Serial.println("[Time] SNTP started");
  }
  timeServiceSetZone(timeZone);
}

bool timeServiceSetZone(const String &timeZone) {
  TimeZone candidate = g_zoneManager.createForZoneName(timeZone.c_str());
  if (candidate.isError()) {
    Serial.printf("[Time] unsupported zone %s, using %s\n",
                  timeZone.c_str(), DEFAULT_TIME_ZONE);
    candidate = g_zoneManager.createForZoneName(DEFAULT_TIME_ZONE);
    g_timeZoneName = DEFAULT_TIME_ZONE;
    g_timeZone = candidate;
    return false;
  }

  g_timeZone = candidate;
  g_timeZoneName = timeZone;
  Serial.printf("[Time] zone=%s\n", g_timeZoneName.c_str());
  return true;
}

bool timeServiceIsZoneSupported(const String &timeZone) {
  return !g_validationZoneManager
              .createForZoneName(timeZone.c_str())
              .isError();
}

bool timeServiceLocalMinute(uint16_t &minuteOfDay) {
  time_t now = time(nullptr);
  if (now < MIN_VALID_UNIX_TIME || g_timeZone.isError()) return false;

  ZonedDateTime local =
      ZonedDateTime::forUnixSeconds64(static_cast<int64_t>(now), g_timeZone);
  if (local.isError()) return false;

  minuteOfDay = static_cast<uint16_t>(
      static_cast<uint16_t>(local.hour()) * 60U + local.minute());
  return true;
}

void timeServicePrintZones(Print &output) {
  output.print('[');
  for (uint16_t i = 0; i < zonedb2025::kZoneAndLinkRegistrySize; ++i) {
    if (i > 0) output.print(',');
    output.print('"');
    BasicZone zone(zonedb2025::kZoneAndLinkRegistry[i]);
    zone.printNameTo(output);
    output.print('"');
  }
  output.print(']');
}
