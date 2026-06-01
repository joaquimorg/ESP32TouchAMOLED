# Desk Companion — ESP32-S3 Touch AMOLED 2.06

🌐 **Português** · [English](README.en.md)

> Um *companheiro de secretária*: um pequeno dispositivo com ecrã **AMOLED** que
> recebe informação do PC por **Bluetooth LE** e mostra **horas** e **notificações**
> (com ícone, título e texto) num visual **minimal tech**, com cuidados anti
> *burn-in* próprios de painéis AMOLED.

O projeto tem duas metades que falam entre si por BLE:

- **Firmware** (ESP-IDF + LVGL) que corre na placa Waveshare.
- **App de PC** (Python) que liga ao dispositivo e reencaminha as **notificações
  do Windows** (título da app + texto + ícone).

---

## ✨ Funcionalidades

- ⏰ **Relógio** `HH:MM` com **segundos destacados**, que **muda de posição**
  periodicamente com animação suave (anti *burn-in*).
- 🔔 **Notificações ricas**: ícone + título + corpo, em cartão central com
  moldura de acento; o ícone aparece numa *badge* com **anel + brilho** subtil.
- 📨 **Mensagens** de texto simples (ficam no ecrã até se tocar).
- 🔋 **Indicador de bateria** (lido do PMU AXP2101).
- 🔌 **Botão PWR** com contador 3→0 em overlay (desligar é feito em hardware).
- 🖥️ **Serviço de PC** com *auto-reconnect* e reencaminhamento das notificações
  do Windows.
- 🎨 Tema **minimal tech**: fundo preto, acento *cyan-teal* único e fonte
  **Chakra Petch** (com acentos PT) gerada à medida.

---

## 🧩 Hardware

| Componente | Detalhe |
| --- | --- |
| Placa | **Waveshare ESP32-S3-Touch-AMOLED-2.06** |
| Ecrã | AMOLED 2.06", **410×502**, controlador **SH8601** (QSPI) |
| Touch | **FT5x06** (I²C) |
| PMU | **AXP2101** (I²C `0x34`) — bateria, PWRKEY, *shutdown* por *long-press* em HW |
| Áudio | codec ES8311 (saída) + ES7210 (mic), I²S (não usado nesta fase) |
| Outros | microSD (SDMMC 1-bit), RTC PCF85063, **PSRAM octal** |
| I²C partilhado | SDA=GPIO15, SCL=GPIO14, 400 kHz |

> ℹ️ O IRQ do AXP2101 está no GPIO35 (colide com a PSRAM octal), por isso o
> estado do PWRKEY lê-se por **polling I²C**, não por interrupção.

---

## 🏗️ Arquitetura

```text
   ┌─────────────────────────┐        Bluetooth LE (GATT)        ┌──────────────────────────┐
   │        PC (Windows)      │  ───────────────────────────────▶ │   ESP32-S3 (firmware)     │
   │                          │   Hora · Mensagem · Notificação   │                            │
   │  pc_app/ (Python+bleak)  │                                   │  main/ (ESP-IDF + LVGL)    │
   │  • lê notificações Win   │                                   │  • servidor GATT (NimBLE)  │
   │  • renderiza ícone RGB565│                                   │  • UI: relógio / notif.    │
   │  • auto-reconnect 30 s   │                                   │  • AXP2101 (bateria/PWR)   │
   └─────────────────────────┘                                   └──────────────────────────┘
                         contrato partilhado: BLE_SERVICES.md (UUIDs + framing)
```

- O dispositivo é **peripheral/server**; o PC é **central/client**.
- O **contrato** entre os dois (UUIDs e formato dos dados) está em
  [`BLE_SERVICES.md`](BLE_SERVICES.md) — **fonte de verdade**: ao mudar o GATT,
  atualizar firmware **e** `pc_app/` **e** este documento.

---

## 🎨 Visual & UI

- **Tema:** fundo **preto** AMOLED (pixels apagados = poupança), texto branco e um
  **acento único** *cyan-teal* (`#35C9DE`).
- **Fonte:** **Chakra Petch** (tech), gerada com `lv_font_conv` em
  [`main/fonts/`](main/fonts/), com **acentos PT** incluídos (range `0x20-0x7F` +
  `0xA0-0xFF`). As fontes Montserrat foram desativadas para poupar flash.
- **Relógio:** `HH:MM` grande + segundos pequenos em acento, linha fina de acento
  e bateria por baixo. Move-se de tempos a tempos (anti *burn-in*).
- **Notificações:** cabeçalho `NOTIFICAÇÃO`/`MENSAGEM` em maiúsculas espaçadas,
  cartão central com moldura de acento, *fade-in* de entrada e, quando há ícone,
  uma *badge* com anel de acento e **glow** subtil.

---

## 📁 Estrutura do repositório

```text
.
├─ CMakeLists.txt          # projeto ESP-IDF: project(desk_companion)
├─ sdkconfig.defaults      # config base (fonte de verdade; sdkconfig é regenerável)
├─ partitions.csv          # app 8M + storage 7M
├─ BLE_SERVICES.md         # protocolo BLE/GATT (contrato firmware ↔ PC)
├─ CLAUDE.md               # notas de contexto/decisões do projeto
├─ main/                   # FIRMWARE (ESP32-S3)
│  ├─ main.c               # app_main, UI/estados, botão PWR
│  ├─ ble_service.c/.h     # servidor GATT NimBLE
│  ├─ power_button.c/.h    # driver mínimo AXP2101 (PWRKEY + estado de energia)
│  ├─ fonts/               # fontes Chakra Petch geradas (+ _ttf/ de origem)
│  └─ CMakeLists.txt
├─ managed_components/     # dependências geridas (NÃO editar)
└─ pc_app/                 # SOFTWARE DO PC (cliente BLE em Python)
   ├─ desk_companion.py    # CLI (scan/pair/forget/time/msg/demo/run)
   ├─ service.py           # serviço persistente (auto-reconnect + notificações)
   ├─ ble_client.py        # wrapper BLE (bleak)
   ├─ win_notifications.py # leitura das notificações do Windows (PyWinRT)
   ├─ protocol.py/config.py# UUIDs+framing / dispositivo guardado
   ├─ start.ps1 / start.bat# arranque rápido (cria venv + instala + corre)
   └─ README.md            # documentação detalhada da app do PC
```

> **Onde mexer:** firmware → raiz/`main/` (build com ESP-IDF). Software do PC →
> **só** `pc_app/` (Python). Nunca editar `managed_components/`.

---

## 🔧 Firmware — compilar e gravar

**Pré-requisitos:** [ESP-IDF](https://docs.espressif.com/projects/esp-idf/) (v5.x)
instalado e no ambiente.

```powershell
# A partir da raiz do projeto, com o ambiente ESP-IDF ativo:
idf.py set-target esp32s3      # só na primeira vez
idf.py build
idf.py -p COMx flash monitor   # substituir COMx pela porta da placa
```

- `build/` e `sdkconfig` são **regeneráveis**; `sdkconfig.defaults` é a fonte de
  verdade. Se mexeres em config de fontes/partições, corre `idf.py fullclean` antes.
- **Regenerar fontes** (precisa de [`lv_font_conv`](https://github.com/lvgl/lv_font_conv)):
  ver instruções no topo de [`main/fonts/fonts.h`](main/fonts/fonts.h).

---

## 🖥️ App de PC — arranque rápido

**Pré-requisitos:** Windows com Bluetooth LE e **Python 3.10–3.13** (o
reencaminhamento de notificações usa PyWinRT).

```powershell
cd pc_app
.\start.ps1            # cria .venv, instala dependências e arranca o serviço
# ou duplo-clique em start.bat
```

Comandos da CLI (equivalente a `python desk_companion.py <cmd>`):

```powershell
.\start.ps1 scan       # listar dispositivos BLE
.\start.ps1 pair       # escolher e GUARDAR o dispositivo
.\start.ps1 time       # acertar a hora (hora local do PC)
.\start.ps1 msg "Olá"  # enviar uma mensagem de texto
.\start.ps1 demo       # acerta hora + mensagem de teste
.\start.ps1 run        # SERVIÇO: reencaminha notificações do Windows
```

- 🧪 **Testar sem app real:** `pc_app/simular_notificacoes.ps1` dispara uma
  notificação do Windows (via BurntToast) que o serviço `run` deteta e reencaminha.
- Detalhes completos em [`pc_app/README.md`](pc_app/README.md).

---

## 📡 Protocolo BLE (resumo)

Serviço `e3f10000-…` com três características de **escrita**:

| Característica | UUID | Payload |
| --- | --- | --- |
| **Hora** | `e3f10001-…` | `uint32` LE = epoch da hora **local** → `settimeofday` |
| **Mensagem** | `e3f10002-…` | texto UTF-8 (até ~240 B) |
| **Notificação** | `e3f10003-…` | *frames* `[opcode\|payload]` (ícone RGB565 + título + corpo) |

*Framing* da notificação: `ICON_BEGIN` → `ICON_DATA…` → `TEXT` → `COMMIT`
(ou só `TEXT` → `COMMIT` sem ícone); `CLEAR` volta ao relógio. Ícone **RGB565**
até **96×96**, pré-renderizado no PC. Especificação completa em
[`BLE_SERVICES.md`](BLE_SERVICES.md).

> ⚠️ **Sem segurança/bonding** nesta fase (ligação aberta). A **hora não persiste**
> sem RTC/NTP: após reboot conta do arranque até nova escrita.

---

## 🗺️ Roadmap

### Fase 1 (implementada) ✅

Boas-vindas → espera por BLE → modo relógio · BLE (hora/mensagem/notificação) ·
notificações do Windows com auto-reconnect · visual minimal tech.

### Fases futuras (a definir)

- Imagem genérica (não só ícone de notificação).
- *Notify* BLE dispositivo → PC (estado/bateria).
- Segurança BLE (bonding/encriptação).
- Persistir hora com RTC PCF85063.
- GUI para a app do PC (por agora só CLI).

---

## 📝 Notas & créditos

- Stack: **ESP-IDF** + **LVGL ~9.2** + `esp_lvgl_port` + BSP
  `waveshare/esp32_s3_touch_amoled_2_06`. LVGL **não é thread-safe** → as chamadas
  `lv_...` são protegidas com `bsp_display_lock/unlock`.
- Fonte **Chakra Petch** © Cadson Demak, sob *SIL Open Font License 1.1*.
- Idioma de trabalho do projeto: **Português (PT)**. Ver [`CLAUDE.md`](CLAUDE.md)
  para o histórico de decisões e contexto.

---

## 📄 Licença

Este projeto é distribuído sob a licença **MIT** — ver [`LICENSE`](LICENSE).

As dependências geridas (`managed_components/`) e a fonte **Chakra Petch**
mantêm as suas próprias licenças (LVGL: MIT; Chakra Petch: SIL OFL 1.1).
