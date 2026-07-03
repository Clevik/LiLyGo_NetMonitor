#include <Arduino.h>
#include <Preferences.h>
#include "settings.h"
#include "net/time_service.h"

static bool hasText(const String &value) {
  for (size_t i = 0; i < value.length(); ++i) {
    char c = value.charAt(i);
    if (c != ' ' && c != '\t' && c != '\r' && c != '\n') {
      return true;
    }
  }
  return false;
}

static bool isDigitChar(char c) {
  return c >= '0' && c <= '9';
}

static bool isAlphaNumChar(char c) {
  return (c >= '0' && c <= '9') ||
         (c >= 'A' && c <= 'Z') ||
         (c >= 'a' && c <= 'z');
}

static bool isValidRouterApiInterface(const String &value) {
  if (value.length() == 0 || value.length() > 63) {
    return false;
  }
  for (size_t i = 0; i < value.length(); ++i) {
    char c = value.charAt(i);
    if (!isAlphaNumChar(c) && c != '.' && c != '_' && c != '-') {
      return false;
    }
  }
  return true;
}

static bool isStrictIPv4Address(const String &value) {
  if (value.length() == 0) {
    return false;
  }

  uint8_t dots = 0;
  uint16_t octet = 0;
  uint8_t digits = 0;

  for (size_t i = 0; i < value.length(); ++i) {
    char c = value.charAt(i);
    if (isDigitChar(c)) {
      octet = static_cast<uint16_t>(octet * 10 + (c - '0'));
      if (octet > 255) {
        return false;
      }
      if (++digits > 3) {
        return false;
      }
      continue;
    }

    if (c == '.') {
      if (digits == 0 || ++dots > 3) {
        return false;
      }
      octet = 0;
      digits = 0;
      continue;
    }

    return false;
  }

  return dots == 3 && digits > 0;
}

static bool isNumericDotted(const String &value) {
  bool hasDot = false;
  for (size_t i = 0; i < value.length(); ++i) {
    char c = value.charAt(i);
    if (c == '.') {
      hasDot = true;
      continue;
    }
    if (!isDigitChar(c)) {
      return false;
    }
  }
  return hasDot;
}

static bool isValidHostname(const String &value) {
  size_t totalLen = value.length();
  if (totalLen == 0 || totalLen > 253) {
    return false;
  }

  uint8_t labelLen = 0;
  char prev = '\0';

  for (size_t i = 0; i < totalLen; ++i) {
    char c = value.charAt(i);

    if (c == '.') {
      if (labelLen == 0 || prev == '-') {
        return false;
      }
      labelLen = 0;
      prev = c;
      continue;
    }

    if (!isAlphaNumChar(c) && c != '-') {
      return false;
    }
    if (labelLen == 0 && c == '-') {
      return false;
    }
    if (++labelLen > 63) {
      return false;
    }
    prev = c;
  }

  return labelLen > 0 && prev != '-';
}

static bool isValidHostAddress(const String &value) {
  if (isStrictIPv4Address(value)) {
    return true;
  }
  if (isNumericDotted(value)) {
    return false;
  }
  return isValidHostname(value);
}

static bool reject(String *error, const char *message) {
  if (error) {
    *error = message;
  }
  return false;
}

static bool isValidTimeZoneName(const String &value) {
  if (value.length() == 0 || value.length() > 63) return false;
  for (size_t i = 0; i < value.length(); ++i) {
    char c = value.charAt(i);
    if (!isAlphaNumChar(c) && c != '/' && c != '_' &&
        c != '-' && c != '+') {
      return false;
    }
  }
  return true;
}

static bool validatePowerSettings(const PowerSaveSettings &settings,
                                  String *error) {
  if (!isBrightnessLevelSupported(settings.startupBrightness, false)) {
    return reject(error, "Startup Brightness must be 25, 50, 75 or 100");
  }
  if (!isBrightnessLevelSupported(settings.nightBrightness, true)) {
    return reject(error, "Night Brightness must be 0, 25, 50, 75 or 100");
  }
  if (settings.autoOffMinutes < 1 || settings.autoOffMinutes > 1440) {
    return reject(error, "Screen Off Timeout must be 1..1440 min");
  }
  if (settings.nightStartMinute > 1439 ||
      settings.nightEndMinute > 1439) {
    return reject(error, "Brightness Schedule time is invalid");
  }
  if (!isValidTimeZoneName(settings.timeZone)) {
    return reject(error, "Time Zone must be a valid IANA name");
  }
  if (!timeServiceIsZoneSupported(settings.timeZone)) {
    return reject(error, "Time Zone is not supported");
  }
  return true;
}

void normalizeSettings(Settings &settings) {
  settings.routerHost.trim();
  settings.snmpCommunity.trim();
  settings.pingHost.trim();
  settings.ifName.trim();
  settings.routerApiLogin.trim();
  settings.routerApiInterface.trim();
  settings.powerSave.timeZone.trim();
}

bool validateSettings(const Settings &settings, String *error) {
  if (!isDisplayRotationSupported(settings.displayRotation)) {
    return reject(error, "Display Rotation is not supported");
  }
  if (!isColorSchemeSupported(settings.colorScheme)) {
    return reject(error, "Color Scheme is not supported");
  }
  if (!hasText(settings.wifiSsid)) {
    return reject(error, "SSID is empty");
  }
  if (!hasText(settings.routerHost)) {
    return reject(error, "Router IP is empty");
  }
  if (!isStrictIPv4Address(settings.routerHost)) {
    return reject(error, "Router IP must be a valid IPv4 address");
  }
  if (settings.snmpPort < 1) {
    return reject(error, "SNMP Port must be 1..65535");
  }
  if (settings.snmpVersion != SnmpVersion::V1 &&
      settings.snmpVersion != SnmpVersion::V2C) {
    return reject(error, "SNMP Version is invalid");
  }
  if (!hasText(settings.snmpCommunity)) {
    return reject(error, "SNMP Community is empty");
  }
  if (settings.ifIndex == 0) {
    return reject(error, "Interface Index must be greater than 0");
  }
  if (!isValidRouterApiInterface(settings.routerApiInterface)) {
    return reject(
        error,
        "Router API Interface must be 1..63 path-safe characters");
  }
  if (!hasText(settings.pingHost)) {
    return reject(error, "Ping Host is empty");
  }
  if (!isValidHostAddress(settings.pingHost)) {
    return reject(error, "Ping Host must be a valid IPv4 address or hostname");
  }
  if (settings.pingIntervalSec < SETTINGS_INTERVAL_MIN_SEC ||
      settings.pingIntervalSec > SETTINGS_INTERVAL_MAX_SEC) {
    return reject(error, "External Ping Interval must be 1..3600 sec");
  }
  if (settings.updateIntervalSec < SETTINGS_INTERVAL_MIN_SEC ||
      settings.updateIntervalSec > SETTINGS_INTERVAL_MAX_SEC) {
    return reject(error, "SNMP Poll Interval must be 1..3600 sec");
  }
  if (settings.rciIntervalSec < SETTINGS_INTERVAL_MIN_SEC ||
      settings.rciIntervalSec > SETTINGS_INTERVAL_MAX_SEC) {
    return reject(error, "Router API Poll Interval must be 1..3600 sec");
  }
  if (settings.wifiRetryDelaySec < SETTINGS_WIFI_RETRY_MIN_SEC ||
      settings.wifiRetryDelaySec > SETTINGS_WIFI_RETRY_MAX_SEC) {
    return reject(error, "Wi-Fi Retry Delay must be 1..3600 sec");
  }
  if (!validatePowerSettings(settings.powerSave, error)) {
    return false;
  }
  return true;
}

namespace SettingsStore {

static constexpr const char *KEY_SCHEMA_VERSION = "schemaVersion";
static constexpr const char *POWER_NAMESPACE = "netmon_power";

static void loadPowerSettings(PowerSaveSettings &out) {
  Preferences prefs;
  if (!prefs.begin(POWER_NAMESPACE, true)) return;

  uint8_t version = prefs.getUChar("ver", 0);
  if (version == 0) {
    prefs.end();
    return;
  }
  if (version != POWER_SETTINGS_SCHEMA_VERSION) {
    prefs.end();
    Serial.printf("[Settings] unsupported Power Safe schema: %u\n",
                  static_cast<unsigned>(version));
    out = PowerSaveSettings{};
    return;
  }

  out.startupBrightness = static_cast<BrightnessLevel>(
      prefs.getUChar("startup", 100));
  out.autoOffEnabled = prefs.getBool("autoOff", false);
  out.autoOffMinutes = prefs.getUShort("offMin", 5);
  out.scheduleEnabled = prefs.getBool("sched", false);
  out.nightStartMinute = prefs.getUShort("nStart", 23 * 60);
  out.nightEndMinute = prefs.getUShort("nEnd", 7 * 60);
  out.nightBrightness = static_cast<BrightnessLevel>(
      prefs.getUChar("nBright", 25));
  out.timeZone = prefs.getString("tz", "Europe/Moscow");
  prefs.end();

  if (!isValidTimeZoneName(out.timeZone) ||
      !timeServiceIsZoneSupported(out.timeZone)) {
    Serial.printf("[Settings] unsupported saved time zone %s; using Europe/Moscow\n",
                  out.timeZone.c_str());
    out.timeZone = "Europe/Moscow";
  }

  String error;
  if (!validatePowerSettings(out, &error)) {
    Serial.printf("[Settings] invalid Power Safe config: %s; using defaults\n",
                  error.c_str());
    out = PowerSaveSettings{};
  }
}

static bool savePowerSettings(const PowerSaveSettings &settings) {
  String error;
  if (!validatePowerSettings(settings, &error)) {
    Serial.printf("[Settings] Power Safe save rejected: %s\n", error.c_str());
    return false;
  }

  Preferences prefs;
  if (!prefs.begin(POWER_NAMESPACE, false)) return false;
  bool ok = true;
  ok = ok && prefs.putUChar("ver", POWER_SETTINGS_SCHEMA_VERSION) == 1;
  ok = ok && prefs.putUChar(
      "startup", static_cast<uint8_t>(settings.startupBrightness)) == 1;
  ok = ok && prefs.putBool("autoOff", settings.autoOffEnabled) == 1;
  ok = ok && prefs.putUShort("offMin", settings.autoOffMinutes) == 2;
  ok = ok && prefs.putBool("sched", settings.scheduleEnabled) == 1;
  ok = ok && prefs.putUShort("nStart", settings.nightStartMinute) == 2;
  ok = ok && prefs.putUShort("nEnd", settings.nightEndMinute) == 2;
  ok = ok && prefs.putUChar(
      "nBright", static_cast<uint8_t>(settings.nightBrightness)) == 1;
  ok = ok && prefs.putString("tz", settings.timeZone) ==
                 settings.timeZone.length();
  prefs.end();
  return ok;
}

bool load(Settings &out) {
  Preferences prefs;
  prefs.begin("netmon", true);

  if (!prefs.getBool("configured", false)) {
    prefs.end();
    return false;
  }

  uint16_t schemaVersion = prefs.getUShort(KEY_SCHEMA_VERSION, 0);
  if (schemaVersion < 1 || schemaVersion > CONFIG_SCHEMA_VERSION) {
    prefs.end();
    Serial.printf("[Settings] incompatible schema version: stored=%u expected=%u\n",
                  schemaVersion, CONFIG_SCHEMA_VERSION);
    clear();
    out = Settings{};
    out.configured = false;
    return false;
  }

  out.wifiSsid       = prefs.getString("ssid", "");
  out.wifiPassword   = prefs.getString("wpass", "");
  out.wifiRetryDelaySec = prefs.getULong("wretry", 20);
  out.routerHost     = prefs.getString("rtr", "");
  out.snmpPort       = prefs.getUShort("sport", 161);
  out.snmpVersion    = static_cast<SnmpVersion>(prefs.getUChar("sver", 1));
  out.snmpCommunity  = prefs.getString("scom", "public");
  out.ifIndex        = prefs.getULong("ifidx", 0);
  out.ifName         = prefs.getString("ifnam", "");
  out.routerApiLogin = prefs.getString("apilogin", "");
  out.routerApiPassword = prefs.getString("apipass", "");
  out.routerApiInterface = prefs.getString(
      "apiif", DEFAULT_ROUTER_API_INTERFACE);
  out.rciIntervalSec = prefs.getULong("rciintv", DEFAULT_RCI_INTERVAL_SEC);
  out.pingHost       = prefs.getString("ping", "8.8.8.8");
  out.pingIntervalSec = prefs.getULong("pintv", 5);
  out.updateIntervalSec = prefs.getULong("intv", 5);
  out.displayRotation = prefs.getUShort(
      "drot", DEFAULT_DISPLAY_ROTATION);
  out.colorScheme = static_cast<ColorScheme>(prefs.getUChar(
      "theme", static_cast<uint8_t>(DEFAULT_COLOR_SCHEME)));

  prefs.end();
  loadPowerSettings(out.powerSave);

  out.configured     = true;
  if (!isDisplayRotationSupported(out.displayRotation)) {
    Serial.printf("[Settings] unsupported display rotation %u, using %u\n",
                  static_cast<unsigned>(out.displayRotation),
                  static_cast<unsigned>(DEFAULT_DISPLAY_ROTATION));
    out.displayRotation = DEFAULT_DISPLAY_ROTATION;
  }
  if (!isColorSchemeSupported(out.colorScheme)) {
    Serial.printf("[Settings] unsupported color scheme %u, using Default\n",
                  static_cast<unsigned>(
                      static_cast<uint8_t>(out.colorScheme)));
    out.colorScheme = DEFAULT_COLOR_SCHEME;
  }
  normalizeSettings(out);

  String error;
  if (!validateSettings(out, &error)) {
    Serial.printf("[Settings] invalid config: %s\n", error.c_str());
    clear();
    out = Settings{};
    out.configured = false;
    return false;
  }

  return true;
}

bool save(const Settings &in) {
  Settings stored = in;
  normalizeSettings(stored);

  String error;
  if (!validateSettings(stored, &error)) {
    Serial.printf("[Settings] save rejected: %s\n", error.c_str());
    return false;
  }

  Preferences prefs;
  prefs.begin("netmon", false);

  prefs.putBool("configured", true);
  prefs.putUShort(KEY_SCHEMA_VERSION, CONFIG_SCHEMA_VERSION);
  prefs.putString("ssid",  stored.wifiSsid);
  prefs.putString("wpass", stored.wifiPassword);
  prefs.putULong("wretry", stored.wifiRetryDelaySec);
  prefs.putString("rtr",   stored.routerHost);
  prefs.putUShort("sport", stored.snmpPort);
  prefs.putUChar("sver",   static_cast<uint8_t>(stored.snmpVersion));
  prefs.putString("scom",  stored.snmpCommunity);
  prefs.putULong("ifidx",  stored.ifIndex);
  prefs.putString("ifnam", stored.ifName);
  prefs.putString("apilogin", stored.routerApiLogin);
  prefs.putString("apipass", stored.routerApiPassword);
  prefs.putString("apiif", stored.routerApiInterface);
  prefs.putULong("rciintv", stored.rciIntervalSec);
  prefs.putString("ping",  stored.pingHost);
  prefs.putULong("pintv",  stored.pingIntervalSec);
  prefs.putULong("intv",   stored.updateIntervalSec);
  prefs.putUShort("drot",  stored.displayRotation);
  prefs.putUChar("theme",  static_cast<uint8_t>(stored.colorScheme));

  prefs.end();
  return savePowerSettings(stored.powerSave);
}

void clear() {
  Preferences prefs;
  prefs.begin("netmon", false);
  prefs.clear();
  prefs.end();
  prefs.begin(POWER_NAMESPACE, false);
  prefs.clear();
  prefs.end();
}

}
