#include <Arduino.h>
#include <Preferences.h>
#include "settings.h"

namespace SettingsStore {

bool load(Settings &out) {
  Preferences prefs;
  prefs.begin("netmon", true);

  if (!prefs.getBool("configured", false)) {
    prefs.end();
    return false;
  }

  out.wifiSsid       = prefs.getString("ssid", "");
  out.wifiPassword   = prefs.getString("wpass", "");
  out.routerHost     = prefs.getString("rtr", "");
  out.snmpPort       = prefs.getUShort("sport", 161);
  out.snmpVersion    = static_cast<SnmpVersion>(prefs.getUChar("sver", 1));
  out.snmpCommunity  = prefs.getString("scom", "public");
  out.ifIndex        = prefs.getULong("ifidx", 0);
  out.ifName         = prefs.getString("ifnam", "");
  out.pingHost       = prefs.getString("ping", "8.8.8.8");
  out.pingIntervalSec = prefs.getULong("pintv", 5);
  out.updateIntervalSec = prefs.getULong("intv", 5);
  out.historyPoints  = prefs.getUShort("hpts", 120);
  out.configured     = true;

  prefs.end();
  return true;
}

bool save(const Settings &in) {
  Preferences prefs;
  prefs.begin("netmon", false);

  prefs.putBool("configured", true);
  prefs.putString("ssid",  in.wifiSsid);
  prefs.putString("wpass", in.wifiPassword);
  prefs.putString("rtr",   in.routerHost);
  prefs.putUShort("sport", in.snmpPort);
  prefs.putUChar("sver",   static_cast<uint8_t>(in.snmpVersion));
  prefs.putString("scom",  in.snmpCommunity);
  prefs.putULong("ifidx",  in.ifIndex);
  prefs.putString("ifnam", in.ifName);
  prefs.putString("ping",  in.pingHost);
  prefs.putULong("pintv",  in.pingIntervalSec);
  prefs.putULong("intv",   in.updateIntervalSec);
  prefs.putUShort("hpts",  in.historyPoints);

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
