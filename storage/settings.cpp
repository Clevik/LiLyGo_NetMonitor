#include <Arduino.h>
#include <Preferences.h>
#include "settings.h"

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

void normalizeSettings(Settings &settings) {
  settings.routerHost.trim();
  settings.snmpCommunity.trim();
  settings.pingHost.trim();
  settings.ifName.trim();
  settings.routerApiLogin.trim();
}

bool validateSettings(const Settings &settings, String *error) {
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
  if (settings.wifiRetryDelaySec < SETTINGS_WIFI_RETRY_MIN_SEC ||
      settings.wifiRetryDelaySec > SETTINGS_WIFI_RETRY_MAX_SEC) {
    return reject(error, "Wi-Fi Retry Delay must be 1..3600 sec");
  }
  return true;
}

namespace SettingsStore {

static constexpr const char *KEY_SCHEMA_VERSION = "schemaVersion";

bool load(Settings &out) {
  Preferences prefs;
  prefs.begin("netmon", true);

  if (!prefs.getBool("configured", false)) {
    prefs.end();
    return false;
  }

  uint16_t schemaVersion = prefs.getUShort(KEY_SCHEMA_VERSION, 0);
  if (schemaVersion != 1 && schemaVersion != CONFIG_SCHEMA_VERSION) {
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
  out.pingHost       = prefs.getString("ping", "8.8.8.8");
  out.pingIntervalSec = prefs.getULong("pintv", 5);
  out.updateIntervalSec = prefs.getULong("intv", 5);

  prefs.end();

  out.configured     = true;
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
  prefs.putString("ping",  stored.pingHost);
  prefs.putULong("pintv",  stored.pingIntervalSec);
  prefs.putULong("intv",   stored.updateIntervalSec);

  prefs.end();
  return true;
}

void clear() {
  Preferences prefs;
  prefs.begin("netmon", false);
  prefs.clear();
  prefs.end();
}

}
