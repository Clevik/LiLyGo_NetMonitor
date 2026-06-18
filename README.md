# NETMONITOR

**[English](#english)** | **[Русский](#русский)**

---

## English

### About

**NETMONITOR** is an autonomous network monitoring device built on the
**LilyGo T-Display S3 AMOLED** platform (ESP32-S3, 1.91″ 536×240 AMOLED).

The device periodically polls your router via **SNMP** and pings an external
server via **ICMP**, then displays real-time WAN status on the built-in screen:

- Router IP address
- WAN link status — **UP** (green) / **DOWN** (red)
- Ping latency to an external host (ms)
- Incoming and outgoing traffic (Mbps)
- Traffic history graph

### First-time Setup

On first boot the device starts its own Wi-Fi access point (`NETMONITOR-XXXX`)
with a captive portal. Connect from your phone or laptop and configure:

- Wi-Fi network (SSID & password, with scan)
- Router IP and SNMP parameters (port, version v1/v2c, community string, interface index)
- Ping host and interval

After saving, the device connects to your Wi-Fi and enters monitoring mode.

### Runtime Settings

While running, the device hosts a web interface at its IP address (shown on
screen as "OTA") where you can adjust SNMP and ping settings without
re-entering Wi-Fi credentials. Firmware OTA updates are also available at
`/ota`.

### Controls

| Action | Function |
|--------|----------|
| Hold KEY button > 3 sec | Reset configuration, return to AP setup mode |
| Reset button | Reboot device |

### Build & Flash

```bash
pio run -t upload                  # build and flash via USB
pio run -t upload -t monitor       # flash + serial monitor
pio device monitor                 # serial monitor only
```

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
| Wi-Fi Retry Delay | 20 sec | Wait between Wi-Fi reconnection attempts |

---

## Русский

### О проекте

**NETMONITOR** — автономное устройство мониторинга сети на базе
**LilyGo T-Display S3 AMOLED** (ESP32-S3, экран 1.91″ 536×240 AMOLED).

Устройство периодически опрашивает роутер по **SNMP** и пингует внешний сервер
по **ICMP**, отображая на экране актуальное состояние WAN-канала:

- IP-адрес роутера
- Статус внешнего соединения — **UP** (зелёный) / **DOWN** (красный)
- Задержка пинга до внешнего хоста (мс)
- Входящий и исходящий трафик (Мбит/с)
- График истории трафика

### Первый запуск

При первом включении устройство создаёт собственную точку доступа Wi-Fi
(`NETMONITOR-XXXX`) с captive-порталом. Подключитесь с телефона или ноутбука
и настройте:

- Сеть Wi-Fi (SSID и пароль, со сканированием)
- IP-адрес роутера и параметры SNMP (порт, версия v1/v2c, community-строка, индекс интерфейса)
- Хост и интервал пинга

После сохранения устройство подключается к вашей Wi-Fi сети и переходит в
режим мониторинга.

### Настройка во время работы

В рабочем режиме устройство размещает веб-интерфейс по своему IP-адресу
(отображается на экране как «OTA»), где можно изменить параметры SNMP и пинга
без повторного ввода данных Wi-Fi. Прошивка по воздуху (OTA) доступна по адресу
`/ota`.

### Управление

| Действие | Функция |
|----------|---------|
| Удержание кнопки KEY > 3 сек | Сброс настроек, возврат в режим настройки |
| Кнопка сброса (RST) | Перезагрузка устройства |

### Сборка и прошивка

```bash
pio run -t upload                  # сборка и прошивка через USB
pio run -t upload -t monitor       # прошивка + последовательный монитор
pio device monitor                 # только монитор порта
```

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
| Задержка реконнекта Wi-Fi | 20 сек | Пауза между попытками переподключения к Wi-Fi |
