// ============================================================
//  NETMONITOR — Конфигурация устройства
//  Хранение/загрузка настроек (NVS via Preferences)
// ============================================================
#pragma once

#include <Arduino.h>

// Версия схемы конфигурации (для будущих миграций).
constexpr uint16_t CONFIG_SCHEMA_VERSION = 3;
constexpr uint32_t SETTINGS_INTERVAL_MIN_SEC = 1;
constexpr uint32_t SETTINGS_INTERVAL_MAX_SEC = 3600;
constexpr uint32_t SETTINGS_WIFI_RETRY_MIN_SEC = 1;
constexpr uint32_t SETTINGS_WIFI_RETRY_MAX_SEC = 3600;
constexpr uint32_t DEFAULT_RCI_INTERVAL_SEC = 15;

inline uint32_t clampSettingsIntervalSec(long value) {
  if (value < static_cast<long>(SETTINGS_INTERVAL_MIN_SEC)) {
    return SETTINGS_INTERVAL_MIN_SEC;
  }
  if (value > static_cast<long>(SETTINGS_INTERVAL_MAX_SEC)) {
    return SETTINGS_INTERVAL_MAX_SEC;
  }
  return static_cast<uint32_t>(value);
}

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
  uint32_t wifiRetryDelaySec = 20;  // пауза между попытками переподключения, сек

  // --- Роутер / SNMP ---
  String      routerHost;          // IPv4-адрес роутера
  uint16_t    snmpPort       = 161;
  SnmpVersion snmpVersion    = SnmpVersion::V2C;
  String      snmpCommunity  = "public";
  uint32_t    ifIndex        = 0;  // индекс WAN-интерфейса (IF-MIB), должен быть > 0
  String      ifName;              // имя интерфейса (опционально)
  String      routerApiLogin;      // логин Keenetic RCI/API для WAN uptime
  String      routerApiPassword;   // пароль Keenetic RCI/API для WAN uptime
  uint32_t    rciIntervalSec = DEFAULT_RCI_INTERVAL_SEC;

  // --- Пинг ---
  String      pingHost       = "8.8.8.8";
  uint32_t    pingIntervalSec = 5;     // интервал пинга, сек

  // --- Обновление / история ---
  uint32_t    updateIntervalSec = 5;   // период опроса SNMP, сек

  // Признак валидной сохранённой конфигурации.
  bool        configured     = false;
};

// Нормализовать строковые поля перед проверкой/сохранением.
void normalizeSettings(Settings& settings);

// Проверить конфигурацию. При ошибке возвращает false и заполняет error.
bool validateSettings(const Settings& settings, String *error = nullptr);

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
