# NETMONITOR

**[English](#english)** | **[Русский](#русский)**

---

## English

### About

**NETMONITOR** is a self-contained ESP32-S3 network monitor for supported
**LILYGO T-Display-S3 AMOLED** boards. It works locally without a cloud
service: the device polls a router as an **SNMP manager**, checks connectivity
with **ICMP**, and renders WAN telemetry on the built-in display.

The dashboard includes:

- Router IP address
- Router system uptime and optional Keenetic WAN connection uptime
- WAN link status verified against ping results, with green/yellow/red health
  indication
- Ping latency and loss for both the router and an external host
- Incoming and outgoing traffic (Mbps)
- Traffic and ping history
- Device IP address for the settings and OTA web interface

### Supported Hardware

| Device | Display / controller | PlatformIO environment |
|--------|----------------------|------------------------|
| LILYGO T-Display-S3 AMOLED 1.91″ | 536×240 rectangular AMOLED, RM67162, CST816T | `LILYGO-T-Display-S3-Sqr` |
| LILYGO T-Display-S3 AMOLED 1.75″ Round | 466×466 round AMOLED, CO5300, CST9217 | `LILYGO-T-Display-S3-1-75-Round` |

Other T-Display-S3 AMOLED sizes and display-controller revisions are not part
of the supported release matrix.

### First-time Setup

On first boot the device starts its own Wi-Fi access point (`NETMONITOR-XXXX`)
with a captive portal. Connect from your phone or laptop and configure:

- Wi-Fi network (SSID & password, with scan)
- Router IP and SNMP parameters (port, version v1/v2c, community string, interface index)
- Optional Keenetic API login/password and the RCI interface name for WAN
  connection uptime
- Ping host and interval
- Display rotation, color scheme, brightness, and power-saving schedule

After saving, the device connects to your Wi-Fi and enters monitoring mode.

### Runtime Settings

While running, the device hosts a web interface at its IP address (shown on
screen as "OTA") where you can adjust SNMP and ping settings without
re-entering Wi-Fi credentials. Firmware OTA updates are also available at
`/ota`.

The `Power Safe` section provides startup brightness, screen auto-off, one
night brightness period, and a searchable IANA time-zone list. Brightness uses
25% steps; monitoring and OTA continue while the display is off.

### Controls

| Action | Function |
|--------|----------|
| Hold KEY button > 3 sec | Reset configuration, return to AP setup mode |
| Reset button | Reboot device |

### Build & Flash

```bash
pio run                                             # build all supported boards
pio run -e LILYGO-T-Display-S3-Sqr                 # build the 1.91" firmware
pio run -e LILYGO-T-Display-S3-1-75-Round          # build the round firmware
pio run -e <environment> -t upload                  # build and flash via USB
pio run -e <environment> -t upload -t monitor       # flash + serial monitor
pio device monitor                                  # serial monitor only
```

The dual-slot OTA layout uses two application partitions and two LittleFS
partitions. Migrating a device from the previous single-LittleFS layout requires
one full USB reflash:

```bash
pio run -e <environment> -t erase
pio run -e <environment> -t uploadfs
pio run -e <environment> -t upload
```

The partition table itself is not migrated by a regular OTA update.

Build artifacts for browser OTA:

```text
.pio/build/<environment>/firmware.bin
.pio/build/<environment>/littlefs.bin
```

The OTA page supports firmware-only updates and atomic firmware + LittleFS
updates. A candidate release is confirmed after a healthy boot; otherwise both
the application and its selected filesystem are rolled back.

### Setup Guides

- [Enabling SNMP on Keenetic routers](docs/SNMP_KEENETIC.md)

### Configuration Defaults

| Setting | Default | Description |
|---------|---------|-------------|
| Ping Host | `8.8.8.8` | Target for ICMP ping |
| Ping Interval | 5 sec | How often to ping |
| SNMP Version | v2c | SNMP protocol version |
| SNMP Community | `public` | Community string |
| SNMP Port | 161 | Router SNMP port |
| Keenetic API Interface | `UsbLte0` | Interface requested through Keenetic RCI |
| RCI Poll Interval | 15 sec | How often to request Keenetic WAN state and uptime |
| Wi-Fi Retry Delay | 20 sec | Wait between Wi-Fi reconnection attempts |
| Startup Brightness | 100% | Normal brightness after boot |
| Screen Auto-off | Disabled | Optional inactivity timeout |
| Night Schedule | Disabled | One local-time night period |
| Night Brightness | 25% | Supports 0/25/50/75/100% |
| Time Zone | `Europe/Moscow` | IANA zone with TZDB/DST rules |

---

## Русский

### О проекте

**NETMONITOR** — автономный сетевой монитор на ESP32-S3 для поддерживаемых плат
**LILYGO T-Display-S3 AMOLED**. Устройство работает локально, без облачного
сервиса: выступает **SNMP-менеджером**, проверяет доступность по **ICMP** и
выводит телеметрию WAN на встроенный дисплей.

На экране отображаются:

- IP-адрес роутера
- UPTIME системы роутера и опциональный UPTIME WAN-соединения Keenetic
- Статус WAN с проверкой по результатам пинга и зелёной, жёлтой или красной
  индикацией состояния
- Задержка и потери пинга до роутера и внешнего хоста
- Входящий и исходящий трафик (Мбит/с)
- История трафика и пинга
- IP-адрес устройства для доступа к настройкам и OTA

### Поддерживаемые устройства

| Устройство | Дисплей / контроллер | Окружение PlatformIO |
|-------------|----------------------|----------------------|
| LILYGO T-Display-S3 AMOLED 1.91″ | Прямоугольный AMOLED 536×240, RM67162, CST816T | `LILYGO-T-Display-S3-Sqr` |
| LILYGO T-Display-S3 AMOLED 1.75″ Round | Круглый AMOLED 466×466, CO5300, CST9217 | `LILYGO-T-Display-S3-1-75-Round` |

Другие размеры T-Display-S3 AMOLED и ревизии контроллеров дисплея не входят в
поддерживаемую релизную матрицу.

### Первый запуск

При первом включении устройство создаёт собственную точку доступа Wi-Fi
(`NETMONITOR-XXXX`) с captive-порталом. Подключитесь с телефона или ноутбука
и настройте:

- Сеть Wi-Fi (SSID и пароль, со сканированием)
- IP-адрес роутера и параметры SNMP (порт, версия v1/v2c, community-строка, индекс интерфейса)
- Опциональные логин/пароль Keenetic API и имя RCI-интерфейса для UPTIME
  WAN-соединения
- Хост и интервал пинга
- Поворот дисплея, цветовую схему, яркость и расписание энергосбережения

После сохранения устройство подключается к вашей Wi-Fi сети и переходит в
режим мониторинга.

### Настройка во время работы

В рабочем режиме устройство размещает веб-интерфейс по своему IP-адресу
(отображается на экране как «OTA»), где можно изменить параметры SNMP и пинга
без повторного ввода данных Wi-Fi. Прошивка по воздуху (OTA) доступна по адресу
`/ota`.

В разделе `Power Safe` настраиваются стартовая яркость, автовыключение экрана,
один ночной период и часовой пояс из стандартного списка IANA. Яркость
задаётся с шагом 25%; мониторинг и OTA продолжают работать при выключенном
дисплее.

### Управление

| Действие | Функция |
|----------|---------|
| Удержание кнопки KEY > 3 сек | Сброс настроек, возврат в режим настройки |
| Кнопка сброса (RST) | Перезагрузка устройства |

### Сборка и прошивка

```bash
pio run                                             # сборка для всех поддерживаемых плат
pio run -e LILYGO-T-Display-S3-Sqr                 # сборка прошивки для 1.91"
pio run -e LILYGO-T-Display-S3-1-75-Round          # сборка прошивки для круглых плат
pio run -e <окружение> -t upload                    # сборка и прошивка через USB
pio run -e <окружение> -t upload -t monitor         # прошивка + монитор порта
pio device monitor                                  # только монитор порта
```

Для OTA с откатом используются два раздела приложения и два раздела LittleFS.
Переход со старой разметки с одним LittleFS требует однократной полной прошивки
по USB:

```bash
pio run -e <окружение> -t erase
pio run -e <окружение> -t uploadfs
pio run -e <окружение> -t upload
```

Обычное OTA-обновление не заменяет таблицу разделов.

Артефакты для загрузки через веб-интерфейс:

```text
.pio/build/<окружение>/firmware.bin
.pio/build/<окружение>/littlefs.bin
```

OTA-страница поддерживает обновление только прошивки и атомарное обновление
прошивки вместе с LittleFS. Новый релиз подтверждается после успешной загрузки;
при сбое приложение и выбранная файловая система откатываются вместе.

### Инструкции по настройке

- [Включение SNMP на роутерах Keenetic](docs/SNMP_KEENETIC.md)

### Конфигурация по умолчанию

| Параметр | По умолчанию | Описание |
|----------|--------------|----------|
| Хост пинга | `8.8.8.8` | Адрес для ICMP-пинга |
| Интервал пинга | 5 сек | Частота пинга |
| Версия SNMP | v2c | Версия протокола SNMP |
| Community SNMP | `public` | Community-строка |
| Порт SNMP | 161 | Порт SNMP на роутере |
| Интерфейс Keenetic API | `UsbLte0` | Интерфейс, запрашиваемый через Keenetic RCI |
| Интервал RCI | 15 сек | Частота запроса состояния и UPTIME WAN Keenetic |
| Задержка реконнекта Wi-Fi | 20 сек | Пауза между попытками переподключения к Wi-Fi |
| Стартовая яркость | 100% | Обычная яркость после запуска |
| Автовыключение экрана | Выключено | Опциональный таймер бездействия |
| Ночное расписание | Выключено | Один период по локальному времени |
| Ночная яркость | 25% | Допустимы 0/25/50/75/100% |
| Часовой пояс | `Europe/Moscow` | IANA-зона с правилами TZDB/DST |
