# Desk Companion — ESP32-S3 Touch AMOLED 2.06

🌐 [Português](README.md) · **English**

> A *desk companion*: a small device with an **AMOLED** screen that receives
> information from the PC over **Bluetooth LE** and shows the **time** and
> **notifications** (with icon, title and text) in a **minimal tech** look, with
> the anti *burn-in* care that AMOLED panels need.

The project has two halves that talk to each other over BLE:

- **Firmware** (ESP-IDF + LVGL) running on the Waveshare board.
- **PC app** (Python) that connects to the device and forwards **Windows
  notifications** (app title + text + icon).

---

## ✨ Features

- ⏰ **Clock** `HH:MM` with **highlighted seconds** that **moves around** the
  screen periodically with a smooth animation (anti *burn-in*).
- 🔔 **Rich notifications**: icon + title + body, in a centered card with an
  accent border; the icon appears in a *badge* with a subtle **ring + glow**.
- 📨 **Plain-text messages** (stay on screen until you tap).
- 🔋 **Battery indicator** (read from the AXP2101 PMU).
- 🔌 **PWR button** with a 3→0 overlay countdown (power-off is done in hardware).
- 🖥️ **PC service** with *auto-reconnect* and Windows notification forwarding.
- 🎨 **Minimal tech** theme: black background, a single *cyan-teal* accent and a
  custom-generated **Chakra Petch** font (with PT accents).

---

## 🧩 Hardware

| Component | Detail |
| --- | --- |
| Board | **Waveshare ESP32-S3-Touch-AMOLED-2.06** |
| Display | AMOLED 2.06", **410×502**, **SH8601** controller (QSPI) |
| Touch | **FT5x06** (I²C) |
| PMU | **AXP2101** (I²C `0x34`) — battery, PWRKEY, *long-press* shutdown in HW |
| Audio | ES8311 codec (out) + ES7210 (mic), I²S (unused in this phase) |
| Other | microSD (SDMMC 1-bit), PCF85063 RTC, **octal PSRAM** |
| Shared I²C | SDA=GPIO15, SCL=GPIO14, 400 kHz |

> ℹ️ The AXP2101 IRQ is on GPIO35 (which collides with the octal PSRAM), so the
> PWRKEY state is read by **I²C polling**, not by interrupt.

---

## 🏗️ Architecture

```text
   ┌─────────────────────────┐        Bluetooth LE (GATT)        ┌──────────────────────────┐
   │        PC (Windows)      │  ───────────────────────────────▶ │   ESP32-S3 (firmware)     │
   │                          │      Time · Message · Notify      │                            │
   │  pc_app/ (Python+bleak)  │                                   │  main/ (ESP-IDF + LVGL)    │
   │  • reads Win notifs      │                                   │  • GATT server (NimBLE)    │
   │  • renders RGB565 icon   │                                   │  • UI: clock / notif.      │
   │  • auto-reconnect 30 s   │                                   │  • AXP2101 (battery/PWR)   │
   └─────────────────────────┘                                   └──────────────────────────┘
                          shared contract: BLE_SERVICES.md (UUIDs + framing)
```

- The device is the **peripheral/server**; the PC is the **central/client**.
- The **contract** between them (UUIDs and data formats) lives in
  [`BLE_SERVICES.md`](BLE_SERVICES.md) — the **source of truth**: when the GATT
  changes, update the firmware **and** `pc_app/` **and** that document.

---

## 🎨 Look & UI

- **Theme:** **black** AMOLED background (off pixels = power saving), white text and
  a **single accent** *cyan-teal* (`#35C9DE`).
- **Font:** **Chakra Petch** (tech), generated with `lv_font_conv` in
  [`main/fonts/`](main/fonts/), including **PT accents** (range `0x20-0x7F` +
  `0xA0-0xFF`). The Montserrat fonts were disabled to save flash.
- **Clock:** large `HH:MM` + small seconds in the accent color, a thin accent line
  and the battery below. Moves around now and then (anti *burn-in*).
- **Notifications:** `NOTIFICAÇÃO`/`MENSAGEM` header in spaced uppercase, a centered
  card with an accent border, a *fade-in* on entry and, when there is an icon, a
  *badge* with an accent ring and a subtle **glow**.

---

## 📁 Repository layout

```text
.
├─ CMakeLists.txt          # ESP-IDF project: project(desk_companion)
├─ sdkconfig.defaults      # base config (source of truth; sdkconfig is regenerable)
├─ partitions.csv          # app 8M + storage 7M
├─ BLE_SERVICES.md         # BLE/GATT protocol (firmware ↔ PC contract)
├─ CLAUDE.md               # project context/decision notes
├─ main/                   # FIRMWARE (ESP32-S3)
│  ├─ main.c               # app_main, UI/state machine, PWR button
│  ├─ ble_service.c/.h     # NimBLE GATT server
│  ├─ power_button.c/.h    # minimal AXP2101 driver (PWRKEY + power status)
│  ├─ fonts/               # generated Chakra Petch fonts (+ source _ttf/)
│  └─ CMakeLists.txt
├─ managed_components/     # managed dependencies (do NOT edit)
└─ pc_app/                 # PC SOFTWARE (BLE client in Python)
   ├─ desk_companion.py    # CLI (scan/pair/forget/time/msg/demo/run)
   ├─ service.py           # persistent service (auto-reconnect + notifications)
   ├─ ble_client.py        # BLE wrapper (bleak)
   ├─ win_notifications.py # reads Windows notifications (PyWinRT)
   ├─ protocol.py/config.py# UUIDs+framing / saved device
   ├─ start.ps1 / start.bat# quick start (create venv + install + run)
   └─ README.md            # detailed PC app documentation
```

> **Where to work:** firmware → root/`main/` (build with ESP-IDF). PC software →
> **only** `pc_app/` (Python). Never edit `managed_components/`.

---

## 🔧 Firmware — build & flash

**Prerequisites:** [ESP-IDF](https://docs.espressif.com/projects/esp-idf/) (v5.x)
installed and in your environment.

```powershell
# From the project root, with the ESP-IDF environment active:
idf.py set-target esp32s3      # first time only
idf.py build
idf.py -p COMx flash monitor   # replace COMx with the board's port
```

- `build/` and `sdkconfig` are **regenerable**; `sdkconfig.defaults` is the source
  of truth. If you touch font/partition config, run `idf.py fullclean` first.
- **Regenerate fonts** (needs [`lv_font_conv`](https://github.com/lvgl/lv_font_conv)):
  see the instructions at the top of [`main/fonts/fonts.h`](main/fonts/fonts.h).

---

## 🖥️ PC app — quick start

**Prerequisites:** Windows with Bluetooth LE and **Python 3.10–3.13** (notification
forwarding uses PyWinRT).

```powershell
cd pc_app
.\start.ps1            # creates .venv, installs dependencies and runs the service
# or double-click start.bat
```

CLI commands (equivalent to `python desk_companion.py <cmd>`):

```powershell
.\start.ps1 scan       # list BLE devices
.\start.ps1 pair       # pick and SAVE the device
.\start.ps1 time       # set the time (PC local time)
.\start.ps1 msg "Hi"   # send a text message
.\start.ps1 demo       # set time + test message
.\start.ps1 run        # SERVICE: forwards Windows notifications
```

- 🧪 **Test without a real app:** `pc_app/simular_notificacoes.ps1` fires a Windows
  notification (via BurntToast) that the `run` service detects and forwards.
- Full details in [`pc_app/README.md`](pc_app/README.md).

---

## 📡 BLE protocol (summary)

Service `e3f10000-…` with three **write** characteristics:

| Characteristic | UUID | Payload |
| --- | --- | --- |
| **Time** | `e3f10001-…` | `uint32` LE = **local** time epoch → `settimeofday` |
| **Message** | `e3f10002-…` | UTF-8 text (up to ~240 B) |
| **Notify** | `e3f10003-…` | *frames* `[opcode\|payload]` (RGB565 icon + title + body) |

Notification framing: `ICON_BEGIN` → `ICON_DATA…` → `TEXT` → `COMMIT` (or just
`TEXT` → `COMMIT` without an icon); `CLEAR` returns to the clock. **RGB565** icon
up to **96×96**, pre-rendered on the PC. Full spec in
[`BLE_SERVICES.md`](BLE_SERVICES.md).

> ⚠️ **No security/bonding** in this phase (open connection). The **time does not
> persist** without RTC/NTP: after a reboot it counts from boot until a new write.

---

## 🗺️ Roadmap

### Phase 1 (implemented) ✅

Welcome → wait for BLE → clock mode · BLE (time/message/notification) · Windows
notifications with auto-reconnect · minimal tech look.

### Future phases (TBD)

- Generic image (not just the notification icon).
- BLE *Notify* device → PC (status/battery).
- BLE security (bonding/encryption).
- Persist the time with the PCF85063 RTC.
- GUI for the PC app (CLI only for now).

---

## 📝 Notes & credits

- Stack: **ESP-IDF** + **LVGL ~9.2** + `esp_lvgl_port` + the
  `waveshare/esp32_s3_touch_amoled_2_06` BSP. LVGL is **not thread-safe** → `lv_...`
  calls are guarded with `bsp_display_lock/unlock`.
- **Chakra Petch** font © Cadson Demak, under the *SIL Open Font License 1.1*.
- Project working language: **Portuguese (PT)**. See [`CLAUDE.md`](CLAUDE.md) for the
  decision history and context.

---

## 📄 License

This project is distributed under the **MIT** license — see [`LICENSE`](LICENSE).

The managed dependencies (`managed_components/`) and the **Chakra Petch** font keep
their own licenses (LVGL: MIT; Chakra Petch: SIL OFL 1.1).
