// ============================================================
//  NETMONITOR — Конфигурация устройства
//  Хранение/загрузка настроек (NVS via Preferences)
// ============================================================
#pragma once

#include <Arduino.h>

// Версия схемы конфигурации (для будущих миграций).
constexpr uint16_t CONFIG_SCHEMA_VERSION = 1;

// Поддерживаемые версии SNMP.
enum class SnmpVersion : uint8_t {
  V1   = 0,
  V2C  = 1,
};

// Структура настроек устройства.
struct Settings {
  // --- Wi-Fi ---
  String wifiSsid;
  String wifiPassword;

  // --- Роутер / SNMP ---
  String      routerHost;          // IP или хост роутера
  uint16_t    snmpPort       = 161;
  SnmpVersion snmpVersion    = SnmpVersion::V2C;
  String      snmpCommunity  = "public";
  uint32_t    ifIndex        = 0;  // индекс WAN-интерфейса (IF-MIB)
  String      ifName;              // имя интерфейса (опционально)

  // --- Пинг ---
  String      pingHost       = "8.8.8.8";
  uint32_t    pingIntervalSec = 5;     // интервал пинга, сек

  // --- Обновление / история ---
  uint32_t    updateIntervalSec = 5;   // период опроса SNMP, сек
  uint16_t    historyPoints     = 120; // точек в графике

  // Признак валидной сохранённой конфигурации.
  bool        configured     = false;
};

// API хранилища настроек.
namespace SettingsStore {

// Загрузить настройки из NVS. Возвращает true, если найдена валидная
// сохранённая конфигурация (configured == true).
bool load(Settings& out);

// Сохранить настройки в NVS. Возвращает true при успехе.
bool save(const Settings& in);

// Полностью очистить сохранённую конфигурацию (сброс к заводским).
void clear();

}  // namespace SettingsStore
