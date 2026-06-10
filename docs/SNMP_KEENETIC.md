# Enabling SNMP on Keenetic Routers / Включение SNMP на роутерах Keenetic

**[English](#english)** | **[Русский](#русский)**

---

## English

### Prerequisites

- Keenetic router with NDMS firmware **3.x** or newer
- Admin access to the router web interface
- NETMONITOR device connected to the same network

### Step 1 — Install SNMP Component

1. Open the router web interface (default: `192.168.1.1`).
2. Go to **Management** → **General** (Управление).
3. In the **System Components** section, find **SNMP** in the list.
4. Click **Install** next to the SNMP component.
5. Wait for the installation to complete. The router may reboot.

> If SNMP is not listed in system components, install it via **OPKG** (Entware):
> 1. Go to **Management** → **OPKG**.
> 2. Install the Entware package repository (if not already installed).
> 3. Connect to the router via SSH: `ssh admin@192.168.1.1`.
> 4. Run: `opkg install snmpd`.

### Step 2 — Configure SNMP Service

1. Go to **Management** → **SNMP** (or **Management** → **Users** → **SNMP** in some firmware versions).
2. Enable the SNMP service by checking **Enable SNMP**.
3. Configure the following parameters:

| Parameter | Recommended Value |
|-----------|-------------------|
| SNMP Version | v2c |
| Community (RO) | `public` (or your custom string) |
| Access | LAN only (or specific IP) |
| Port | 161 (default) |

4. Click **Apply** or **Save**.

### Step 3 — Verify SNMP is Working

Test from a computer on the same network:

```bash
# Install snmp tools if needed (macOS)
brew install net-snmp

# Test SNMP query
snmpwalk -v2c -c public 192.168.1.1 1.3.6.1.2.1.2.2.1
```

You should see a list of interfaces with their statuses and counters.

### Step 4 — Configure NETMONITOR

On the NETMONITOR setup page:

1. **Router IP / Host** — enter your router's IP address (e.g. `192.168.1.1`).
2. **SNMP Port** — `161`.
3. **SNMP Version** — `v2c`.
4. **SNMP Community** — the community string you set (e.g. `public`).
5. **Interface Index** — the specific WAN interface index. It must be greater than `0`.

### Finding the WAN Interface Index

NETMONITOR does not auto-detect the WAN interface in this firmware. Find the
correct interface index before saving the setup form:

```bash
snmpwalk -v2c -c public 192.168.1.1 ifDescr
snmpwalk -v2c -c public 192.168.1.1 ifName
```

Look for the interface named `WAN`, `Ethernet0`, `npu0`, or similar. The index is the number at the end of the OID line (e.g. `IF-MIB::ifDescr.4 = STRING: WAN` means index **4**).

### Troubleshooting

| Problem | Solution |
|---------|----------|
| No response to SNMP | Check that SNMP service is enabled and saved |
| Timeout | Verify the router IP and community string match |
| Wrong interface data | Try a different interface index |
| Component not visible | Update router firmware to the latest version |

---

## Русский

### Предварительные требования

- Роутер Keenetic с прошивкой NDMS **3.x** или новее
- Доступ администратора к веб-интерфейсу роутера
- Устройство NETMONITOR подключено к той же сети

### Шаг 1 — Установка компонента SNMP

1. Откройте веб-интерфейс роутера (по умолчанию: `192.168.1.1`).
2. Перейдите в раздел **Управление** → **Общие**.
3. В разделе **Системные компоненты** найдите **SNMP** в списке.
4. Нажмите **Установить** рядом с компонентом SNMP.
5. Дождитесь завершения установки. Роутер может перезагрузиться.

> Если SNMP отсутствует в списке системных компонентов, установите через **OPKG** (Entware):
> 1. Перейдите в **Управление** → **OPKG**.
> 2. Установите репозиторий пакетов Entware (если ещё не установлен).
> 3. Подключитесь к роутеру по SSH: `ssh admin@192.168.1.1`.
> 4. Выполните: `opkg install snmpd`.

### Шаг 2 — Настройка службы SNMP

1. Перейдите в раздел **Управление** → **SNMP** (в некоторых версиях прошивки: **Управление** → **Пользователи** → **SNMP**).
2. Включите службу SNMP, установив флажок **Включить SNMP**.
3. Настройте следующие параметры:

| Параметр | Рекомендуемое значение |
|----------|------------------------|
| Версия SNMP | v2c |
| Community (RO) | `public` (или ваша строка) |
| Доступ | Только LAN (или конкретный IP) |
| Порт | 161 (по умолчанию) |

4. Нажмите **Применить** или **Сохранить**.

### Шаг 3 — Проверка работы SNMP

Проверьте с компьютера в той же сети:

```bash
# Установите snmp-утилиты (macOS)
brew install net-snmp

# Проверьте SNMP-запрос
snmpwalk -v2c -c public 192.168.1.1 1.3.6.1.2.1.2.2.1
```

Вы должны увидеть список интерфейсов с их статусами и счётчиками.

### Шаг 4 — Настройка NETMONITOR

На странице настройки NETMONITOR:

1. **Router IP / Host** — IP-адрес роутера (например, `192.168.1.1`).
2. **SNMP Port** — `161`.
3. **SNMP Version** — `v2c`.
4. **SNMP Community** — строка community, которую вы задали (например, `public`).
5. **Interface Index** — конкретный индекс WAN-интерфейса. Значение должно быть больше `0`.

### Как найти индекс WAN-интерфейса

В этой версии прошивки NETMONITOR не выполняет автоопределение WAN-интерфейса.
Найдите нужный индекс перед сохранением формы настройки:

```bash
snmpwalk -v2c -c public 192.168.1.1 ifDescr
snmpwalk -v2c -c public 192.168.1.1 ifName
```

Найдите интерфейс с именем `WAN`, `Ethernet0`, `npu0` или подобным. Индекс — это число в конце строки OID (например, `IF-MIB::ifDescr.4 = STRING: WAN` означает индекс **4**).

### Устранение неполадок

| Проблема | Решение |
|----------|---------|
| Нет ответа на SNMP | Проверьте, что служба SNMP включена и настройки сохранены |
| Таймаут | Убедитесь, что IP-адрес и community-строка указаны верно |
| Неверные данные интерфейса | Попробуйте другой индекс интерфейса |
| Компонент не виден | Обновите прошивку роутера до последней версии |
