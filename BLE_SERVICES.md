# Desk Companion — Protocolo BLE (GATT)

Documento de referência para criar a **aplicação do PC/telemóvel** que comunica
com o dispositivo *Desk Companion* (Waveshare ESP32-S3-Touch-AMOLED-2.06).

> Estado: **Fase 1 / base.** Apenas escrita (host → dispositivo). Sem segurança/
> emparelhamento (conexão aberta). A evoluir em fases futuras.

---

## 1. Visão geral

- **Transporte:** Bluetooth Low Energy (BLE), GATT server no dispositivo.
- **Papel:** o dispositivo é **peripheral/server**; o PC é **central/client**.
- **Nome anunciado (advertising):** `Desk Companion`
- **Host stack:** NimBLE (ESP-IDF).
- O dispositivo anuncia continuamente enquanto não estiver ligado; ao desligar,
  volta a anunciar automaticamente.

### Fluxo no dispositivo
1. Ao ligar: ecrã de boas-vindas (~30 s) à espera de ligação.
2. Se um cliente BLE ligar (ou ao fim de 30 s) → **modo relógio** `HH:MM:SS`.
3. Escrever na característica **Hora** acerta o relógio.
4. Escrever na característica **Mensagem** mostra o texto em ecrã inteiro;
   o texto fica visível **até se tocar no ecrã**, momento em que é limpo e
   volta ao relógio.

---

## 2. UUIDs

| Elemento                 | UUID                                   | Propriedades |
|--------------------------|----------------------------------------|--------------|
| **Serviço**              | `e3f10000-8a1b-4c2d-9e5f-1a2b3c4d5e6f` | —            |
| **Hora** (Time)          | `e3f10001-8a1b-4c2d-9e5f-1a2b3c4d5e6f` | Write / Write Without Response |
| **Mensagem** (Msg)       | `e3f10002-8a1b-4c2d-9e5f-1a2b3c4d5e6f` | Write / Write Without Response |
| **Notificação** (Notify) | `e3f10003-8a1b-4c2d-9e5f-1a2b3c4d5e6f` | Write / Write Without Response |

---

## 3. Característica **Hora** — `e3f10001-…`

- **Operação:** Write (com ou sem resposta).
- **Payload:** **4 bytes**, `uint32` **little-endian** = segundos Unix.
- **Convenção de fuso:** envia-se a **hora LOCAL** expressa como epoch, ou seja,
  os segundos desde 1970-01-01 **como se a hora local fosse UTC**. O dispositivo
  mostra diretamente `HH:MM:SS` desse valor (não aplica fusos).

### Como calcular o valor (hora local)
- **Python:**
  ```python
  import time
  # epoch que, lido como UTC, dá a hora local atual:
  local_epoch = int(time.time()) - time.timezone + (time.localtime().tm_isdst and 3600 or 0)
  payload = local_epoch.to_bytes(4, "little")
  ```
  Em alternativa, mais robusto com `datetime`:
  ```python
  from datetime import datetime, timezone
  now = datetime.now()                      # hora local "wall clock"
  local_epoch = int(now.replace(tzinfo=timezone.utc).timestamp())
  payload = local_epoch.to_bytes(4, "little")
  ```

---

## 4. Característica **Mensagem** — `e3f10002-…`

- **Operação:** Write (com ou sem resposta).
- **Payload:** texto **UTF-8**, **até 240 bytes** (sem terminador `\0`).
- **Comportamento:** mostra a mensagem em ecrã inteiro; permanece visível até
  o utilizador **tocar no ecrã**, sendo então limpa e regressando ao relógio.
- **Mensagens longas:** para garantir o envio numa só escrita, negociar **MTU
  ≈ 247** (o dispositivo prefere 247). Com MTU 247 cabem ~244 bytes por escrita.
  Acima do MTU, usar *Write Long* (prepare/execute) — suportado.

---

## 4b. Característica **Notificação** — `e3f10003-…`

Notificação rica: **ícone** (opcional) + **título** (nome da app) + **corpo**.
Como não cabe numa só escrita BLE, usa-se um pequeno **protocolo de frames**:
cada escrita é um frame `[opcode | payload]`.

| Opcode | Nome         | Payload                                   |
|:------:|--------------|-------------------------------------------|
| `0x01` | `TEXT`       | `titulo` + `0x00` + `corpo` (UTF-8)       |
| `0x02` | `ICON_BEGIN` | `width`(u16 LE) + `height`(u16 LE), RGB565|
| `0x03` | `ICON_DATA`  | bytes RGB565 (um pedaço do ícone)         |
| `0x04` | `COMMIT`     | (vazio) → monta e mostra a notificação    |
| `0x05` | `CLEAR`      | (vazio) → limpa e volta ao relógio        |

### Sequência de envio

- **Com ícone:** `ICON_BEGIN` → vários `ICON_DATA` → `TEXT` → `COMMIT`.
- **Sem ícone:** `TEXT` → `COMMIT`.

### Ícone (RGB565)

- Formato **RGB565 little-endian**, linha a linha, topo→baixo (`width*height*2` bytes).
- Dimensão máxima aceite: **96×96** (recomendado 64×64).
- O PC pré-renderiza/redimensiona o ícone (não se envia PNG).
- Cada `ICON_DATA` deve caber numa escrita: até `MTU-4` bytes de payload
  (com MTU 247 → ~243 bytes por frame).
- Para menor latência, enviar `ICON_DATA` com **Write Without Response**; manter
  `ICON_BEGIN`, `TEXT` e `COMMIT` com Write com resposta para preservar ordem e
  confirmação dos frames de controlo.

### Limites de texto

- Título: até **48 bytes** UTF-8. Corpo: até **180 bytes** UTF-8.
- `TEXT` tem de caber numa escrita (título + corpo + 2 ≤ ~240 bytes).

> A notificação fica visível até se **tocar no ecrã** (ou receber `CLEAR`),
> momento em que o dispositivo volta ao relógio.

---

## 5. Exemplo de cliente em Python (`bleak`)

```python
import asyncio
from datetime import datetime, timezone
from bleak import BleakClient, BleakScanner

DEVICE_NAME = "Desk Companion"
UUID_TIME = "e3f10001-8a1b-4c2d-9e5f-1a2b3c4d5e6f"
UUID_MSG  = "e3f10002-8a1b-4c2d-9e5f-1a2b3c4d5e6f"

async def main():
    dev = await BleakScanner.find_device_by_name(DEVICE_NAME, timeout=10)
    if not dev:
        print("Dispositivo não encontrado")
        return

    async with BleakClient(dev) as client:
        # (opcional) pedir MTU maior para mensagens longas
        try:
            await client._backend._acquire_mtu()  # depende do backend/SO
        except Exception:
            pass

        # 1) Acertar a hora (hora local como epoch)
        now = datetime.now()
        local_epoch = int(now.replace(tzinfo=timezone.utc).timestamp())
        await client.write_gatt_char(UUID_TIME, local_epoch.to_bytes(4, "little"), response=True)

        # 2) Enviar uma mensagem
        await client.write_gatt_char(UUID_MSG, "Olá da app do PC!".encode("utf-8"), response=True)

asyncio.run(main())
```

---

## 6. Notas / limitações (base)

- **Sem segurança/emparelhamento** nesta fase — qualquer central pode ligar e
  escrever. A adicionar em fase futura se necessário.
- **Sem notificações** (dispositivo→PC) ainda. Tudo é escrita PC→dispositivo.
- A **hora não persiste** sem RTC/NTP: após reboot volta a contar do arranque
  até nova escrita na característica Hora.
- Funciona com **Web Bluetooth** (Chrome) e **bleak** (Python). Em Web Bluetooth
  declarar o serviço em `optionalServices` ao pedir o dispositivo.

---

## 7. Ideias para fases futuras (a definir)

- Característica de **imagem** (transferência por blocos).
- **Notify** para estado/bateria do dispositivo → PC.
- **Segurança** (bonding/encriptação).
- Tipos de mensagem (notificação, alerta, ícone, duração).
