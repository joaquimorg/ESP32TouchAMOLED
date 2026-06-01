# Desk Companion — ESP32-S3 Touch AMOLED 2.06

> Ficheiro de contexto para interações com IA. Manter atualizado à medida que o projeto evolui.
> Idioma de trabalho: **Português (PT)**.

## 1. Objetivo do projeto

Converter a placa **Waveshare ESP32-S3-Touch-AMOLED-2.06** num *desk companion*: um
dispositivo de secretária que recebe informação de um **PC ou telemóvel** via
**Bluetooth LE** (decidido — poupa bateria) e mostra mensagens, imagens, horas e
outras informações no ecrã AMOLED.

O projeto é construído **por fases**, adicionando funcionalidades incrementalmente.
Muitos pontos de arquitetura ainda estão por decidir — confirmar com o utilizador
antes de assumir.

## 2. Hardware

- **Placa:** Waveshare ESP32-S3-Touch-AMOLED-2.06
- **Ecrã:** AMOLED 2.06", 410×502, controlador SH8601, interface QSPI
- **Touch:** FT5x06 (I2C)
- **PMU (gestão de energia):** **AXP2101** no bus I2C, endereço `0x34`
  - O botão **PWR** está ligado ao PWRKEY do AXP2101.
  - O **shutdown por long-press já é feito em hardware** pelo AXP2101.
  - O pino de IRQ do AXP2101 está no **GPIO35**, que **colide com a PSRAM octal** →
    não se pode usar IRQ por GPIO; o estado do PWRKEY lê-se por **polling I2C**.
- **Áudio:** codec ES8311 (saída) + ES7210 (mic), I2S. PA no GPIO46.
- **Outros:** slot microSD (SDMMC 1-bit), RTC PCF85063, PSRAM octal.
- **I2C partilhado:** SDA=GPIO15, SCL=GPIO14, 400 kHz. Usar sempre
  `bsp_i2c_get_handle()` / `bsp_i2c_init()` para partilhar a bus.

### Notas AMOLED (importante)

- Pixels pretos = apagados → usar **fundo preto** para poupar energia e contraste.
- **Risco de burn-in:** elementos estáticos (ex.: relógio) devem **mover-se
  periodicamente** para não fixar imagem.

## 3. Stack de software

- **Framework:** ESP-IDF (projeto baseou-se no exemplo `lvgl_demo`, já removido).
- **GUI:** LVGL ~9.3 (via `lvgl/lvgl`) + `esp_lvgl_port`.
- **BSP:** componente gerido `waveshare/esp32_s3_touch_amoled_2_06`.
- LVGL **não é thread-safe** → envolver chamadas `lv_...` com
  `bsp_display_lock(0)` / `bsp_display_unlock()`.

### Estrutura

```text
CMakeLists.txt            -> project(desk_companion)
sdkconfig.defaults        -> config base (sem demos, sem perf monitor)
main/
  main.c                  -> app_main, UI, máquina de estados (boas-vindas/relógio/mensagem), botão PWR
  power_button.c/.h       -> driver mínimo AXP2101 (PWRKEY via I2C)
  ble_service.c/.h        -> servidor GATT NimBLE (hora + mensagem + notificação)
  CMakeLists.txt          -> SRCS main.c power_button.c ble_service.c; REQUIRES esp_driver_i2c bt nvs_flash
  idf_component.yml       -> deps: waveshare bsp, lvgl, usb
managed_components/        -> dependências geridas (NÃO editar)
BLE_SERVICES.md           -> protocolo BLE/GATT documentado (fonte de verdade)
pc_app/                    -> SOFTWARE DO PC (cliente BLE Python), NÃO é firmware
  desk_companion.py        -> CLI (scan/pair/forget/time/msg/demo/run)
  service.py               -> serviço persistente (auto-reconnect + notificações)
  ble_client.py            -> wrapper BLE (bleak)
  win_notifications.py     -> notificações do Windows (PyWinRT) + ícone (Pillow)
  protocol.py / config.py  -> UUIDs+framing partilhados / dispositivo guardado
  requirements.txt         -> deps Python (bleak, Pillow, winrt-*)
  README.md                -> doc do software PC (manter atualizada por funcionalidade)
```

### Onde mexer (firmware vs PC)

- **Firmware (dispositivo ESP32-S3):** raiz do projeto — `main/`, `sdkconfig.defaults`,
  `CMakeLists.txt`, `partitions.csv`. Build com ESP-IDF.
- **Software do PC:** **só** dentro de `pc_app/` (Python + `bleak`). Não precisa de
  ESP-IDF; corre no PC.
- **Contrato entre os dois:** `BLE_SERVICES.md` (UUIDs/formatos). Ao mudar o GATT,
  atualizar firmware + `BLE_SERVICES.md` + `pc_app/`.

### Convenções

- Nome do projeto: **`desk_companion`**.
- `build/` e `sdkconfig` são regeneráveis; `sdkconfig.defaults` é a fonte de verdade.
- Tema visual: **fundo preto, texto branco** (AMOLED).
- Não editar nada dentro de `managed_components/`.

## 4. Estado atual (feito)

- Removido todo o código/config do `lvgl_demo` (widgets, music, benchmark, etc.).
- Projeto renomeado para `desk_companion`.
- Desligado o overlay de FPS/perf monitor (`LV_USE_PERF_MONITOR`, `LV_USE_SYSMON`,
  `LV_USE_MEM_MONITOR` explicitamente `is not set` no `sdkconfig.defaults`).
- Tema escuro: fundo preto + texto branco.
- Ecrã inicial mostra **"Welcome to Desk Companion"** centrado.
- **Botão PWR:** ao premir, overlay (painel preto full-screen) com contador grande
  **3→0** (Montserrat 48); largar cancela. Ao chegar a 0, o AXP2101 desliga
  (hardware). Driver em `power_button.c`.
  - ⚠️ A polaridade do edge (press = edge negativo) é assumida; se o contador
    aparecer ao largar, trocar `PRESSED`↔`RELEASED` em `power_button_poll()`.
  - ⚠️ Sincronismo: contador dura 3 s; o tempo de long-press do AXP2101 (reg `0x27`)
    pode diferir (4/6/8/10 s). Alinhar se necessário.
- **Fase 1 implementada** (`main.c`): boas-vindas → espera 30 s por ligação BLE
  (ou entra logo que um cliente liga) → **modo relógio** `HH:MM:SS` (Montserrat 48)
  que muda de posição a cada 30 s com animação suave (anti burn-in). `lv_timer`+`lv_anim`.
- **Bluetooth LE (NimBLE)** em `ble_service.c`: servidor GATT, nome `Desk Companion`.
  - Serviço `e3f10000-…`; **Hora** `e3f10001-…` (Write, `uint32` LE epoch local →
    `settimeofday`); **Mensagem** `e3f10002-…` (Write, texto simples);
    **Notificação** `e3f10003-…` (Write, framing OP_* → ícone+título+corpo).
  - **Notificação/mensagem**: overlay preto full-screen (ícone RGB565 + título +
    corpo); fica até **tocar no ecrã** → volta ao relógio. Ícone máx 96×96, sem
    decoder (RGB565 pré-renderizado no PC). Protocolo em `BLE_SERVICES.md`.
  - ⚠️ **Hora:** sem RTC/NTP, só é acertada por BLE; após reboot conta do arranque
    até nova escrita. Sem segurança/bonding nesta fase.
- **App PC (`pc_app/`)**: serviço `run` que liga ao dispositivo guardado, religa a
  cada 30 s se cair, e reencaminha as **notificações do Windows** (título da app +
  texto + ícone) via PyWinRT. Requer Python 3.10–3.13 (PyWinRT) para as notificações.
- **Tema visual "minimal tech"** (estilo escolhido): fundo preto AMOLED + acento
  único discreto (cyan-teal `CLR_ACCENT 0x35C9DE`). Constantes de paleta no topo de
  `main.c` (`CLR_ACCENT/CLR_TEXT/CLR_TEXT_DIM/CLR_HAIRLINE`).
  - **Relógio:** `HH:MM` grande (Montserrat 48) + **segundos destacados** pequenos
    em acento, tipo superscript (reposicionados a cada tick), linha fina de acento
    por baixo e bateria com tom mais sóbrio. Continua a mover-se (anti burn-in).
  - **Notificações:** cabeçalho em maiúsculas espaçadas (`MENSAGEM`/`NOTIFICACAO`),
    **cartão central** com moldura fina de acento e cantos arredondados, hierarquia
    tipográfica (título 28 branco / corpo 22 cinza), divisória discreta e **fade-in**
    de entrada. Pontos de fila no acento.
  - Contador do botão PWR ganhou anel de acento à volta do número.
- **Fontes:** **Chakra Petch** (tech) gerada com `lv_font_conv` em `main/fonts/`
  (TTF de origem em `main/fonts/_ttf/`, instruções de regeneração no topo de
  `fonts/fonts.h`). Ficheiros: `font_chakra_64` (HH:MM + contador, só `0-9:`),
  `font_chakra_sec_30` (segundos, `0-9`), `font_chakra_title_30` (título/boas-
  vindas), `font_chakra_body_24` (corpo), `font_chakra_18` (cabeçalho/bateria).
  As fontes de texto incluem **acentos PT** (range `0x20-0x7F` + `0xA0-0xFF`) e um
  conjunto de **símbolos monocromáticos** (setas, bullets, ★, ♥, €, ™, ≥, ✓…)
  vindos da **DejaVu Sans** (merge no `lv_font_conv`; ver `fonts/fonts.h`).
  - `sanitize_text()` (antes `sanitize_ascii`) **preserva** Latin-1 e as sequências
    UTF-8 dos símbolos suportados; converte pontuação tipográfica
    (travessões/aspas/reticências) para ASCII; o resto (emoji) é mantido em UTF-8
    mas, sem glifo e com `LV_USE_FONT_PLACEHOLDER` desligado, não é desenhado.
  - **Montserrat desativadas** no `sdkconfig.defaults` (e no `sdkconfig` em cache).
    O `LV_FONT_DEFAULT` (fallback interno do LVGL, não usado na UI) aponta para a
    **UNSCII 8** e `LV_USE_FONT_COMPRESSED` está desligado (as Chakra são
    `--no-compress`). Nenhum componente compilado referencia Montserrat.
  - `main/CMakeLists.txt` compila `fonts/*.c` e define `LV_LVGL_H_INCLUDE_SIMPLE`
    para o componente `main` (os ficheiros gerados incluem o LVGL via `lvgl.h`).

## 5. Roadmap por fases

### Fase 1 (implementada) ✅

1. Ao ligar: mensagem de boas-vindas. ✅
2. Esperar **~30 s** por ligação BLE (ou entrar logo que liga). ✅
3. Sem ligação nesse tempo → **modo relógio** `HH:MM:SS`. ✅
4. Relógio **muda de posição** com **animação de movimento** (anti burn-in). ✅
5. **BLE base**: receber hora + mensagens (mensagem até tocar no ecrã). ✅
6. **Notificações do Windows** (título + texto + ícone) + serviço PC com
   auto-reconnect a cada 30 s. ✅

### Fases futuras (a definir)

- **Imagem genérica** (não só ícone de notificação).
- **Notify** BLE (estado/bateria) dispositivo → PC.
- **Segurança** BLE (bonding/encriptação).
- Persistir hora com RTC PCF85063.
- GUI para a app do PC (por agora só CLI).

## 6. Decisões em aberto (a confirmar com o utilizador)

- **Fonte da hora:** por agora só via BLE. Usar também RTC PCF85063 (persistir
  entre reboots)? NTP fica de fora (sem Wi-Fi).
- **Protocolo de imagem** e outros tipos de conteúdo.
- Tempo exato do timeout (assumido 30 s) e cadência/estilo da animação do relógio.
- Alinhamento do tempo de power-off do AXP2101 com o contador de 3 s.

## 7. Notas para a IA

- Confirmar antes de assumir pontos de arquitetura ainda não decididos.
- Não reintroduzir dependências dos demos LVGL.
- Manter o look AMOLED (preto/branco) e cuidados de burn-in em qualquer UI nova.
- Ambiente: Windows + PowerShell. Build com `idf.py build` (ESP-IDF).
