// ============================================================
//  NETMONITOR — Конфигурация устройства
//  Хранение/загрузка настроек (NVS via Preferences)
// ============================================================
#pragma once

#include <Arduino.h>
#include "display/power_policy.h"

// Версия схемы конфигурации (для будущих миграций).
constexpr uint16_t CONFIG_SCHEMA_VERSION = 6;
constexpr uint8_t POWER_SETTINGS_SCHEMA_VERSION = 1;
constexpr uint32_t SETTINGS_INTERVAL_MIN_SEC = 1;
constexpr uint32_t SETTINGS_INTERVAL_MAX_SEC = 3600;
constexpr uint32_t SETTINGS_WIFI_RETRY_MIN_SEC = 1;
constexpr uint32_t SETTINGS_WIFI_RETRY_MAX_SEC = 3600;
constexpr uint32_t DEFAULT_RCI_INTERVAL_SEC = 15;
constexpr char DEFAULT_ROUTER_API_INTERFACE[] = "UsbLte0";
constexpr uint16_t DEFAULT_DISPLAY_ROTATION = 0;

#if defined(HW_AMOLED_143)
constexpr uint16_t SUPPORTED_DISPLAY_ROTATIONS[] = {0, 90, 180, 270};
#else
constexpr uint16_t SUPPORTED_DISPLAY_ROTATIONS[] = {0, 180};
#endif
constexpr size_t SUPPORTED_DISPLAY_ROTATION_COUNT =
    sizeof(SUPPORTED_DISPLAY_ROTATIONS) /
    sizeof(SUPPORTED_DISPLAY_ROTATIONS[0]);

inline bool isDisplayRotationSupported(uint16_t rotation) {
  for (size_t i = 0; i < SUPPORTED_DISPLAY_ROTATION_COUNT; ++i) {
    if (SUPPORTED_DISPLAY_ROTATIONS[i] == rotation) {
      return true;
    }
  }
  return false;
}

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

enum class ColorScheme : uint8_t {
  Default = 0,
  Cyber,
  Ice,
  Lime,
  Azure,
  Mint,
  Blue,
  Violet,
};

constexpr ColorScheme DEFAULT_COLOR_SCHEME = ColorScheme::Default;
constexpr uint8_t COLOR_SCHEME_COUNT = 8;

inline bool isColorSchemeSupported(ColorScheme scheme) {
  return static_cast<uint8_t>(scheme) < COLOR_SCHEME_COUNT;
}

struct PowerSaveSettings {
  BrightnessLevel startupBrightness = BrightnessLevel::Full;
  bool            autoOffEnabled = false;
  uint16_t        autoOffMinutes = 5;
  bool            scheduleEnabled = false;
  uint16_t        nightStartMinute = 23 * 60;
  uint16_t        nightEndMinute = 7 * 60;
  BrightnessLevel nightBrightness = BrightnessLevel::Quarter;
  String          timeZone = "Europe/Moscow";
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
  String      routerApiLogin;      // логин Keenetic RCI/API для WAN uptime
  String      routerApiPassword;   // пароль Keenetic RCI/API для WAN uptime
  String      ifName = DEFAULT_ROUTER_API_INTERFACE;  // имя интерфейса Router API
  uint32_t    rciIntervalSec = DEFAULT_RCI_INTERVAL_SEC;

  // --- Пинг ---
  String      pingHost       = "8.8.8.8";
  uint32_t    pingIntervalSec = 5;     // интервал пинга, сек

  // --- Обновление / история ---
  uint32_t    updateIntervalSec = 5;   // период опроса SNMP, сек

  // --- Дисплей ---
  uint16_t    displayRotation = DEFAULT_DISPLAY_ROTATION;  // градусы
  ColorScheme colorScheme = DEFAULT_COLOR_SCHEME;
  PowerSaveSettings powerSave;

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
