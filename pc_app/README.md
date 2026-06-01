# Desk Companion — Software para PC

Cliente/ferramentas para comunicar com o dispositivo **Desk Companion**
(ESP32-S3-Touch-AMOLED-2.06) via **Bluetooth LE**.

> **Esta pasta (`pc_app/`) contém APENAS o software do lado do PC.**
> O firmware do dispositivo está na raiz do projeto (`main/`, etc.).
> O protocolo BLE está documentado em [`../BLE_SERVICES.md`](../BLE_SERVICES.md)
> (fonte de verdade — manter alinhado com o firmware).

---

## 1. Requisitos

- **Python 3.10–3.13** (o reencaminhamento de notificações do Windows usa PyWinRT,
  cujos wheels podem ainda não existir para 3.14). Os comandos BLE básicos
  (`scan/pair/time/msg/demo`) funcionam em qualquer 3.10+.
- Adaptador Bluetooth LE no PC.
- [`bleak`](https://github.com/hbldh/bleak) (BLE, multiplataforma) + `Pillow` (ícone).
- **Notificações (só Windows):** pacotes `winrt-*` (PyWinRT) — ver `requirements.txt`.
  É preciso autorizar o acesso às notificações em *Definições > Privacidade e
  segurança > Notificações*.

### Instalação

```powershell
cd pc_app
python -m venv .venv
.\.venv\Scripts\Activate.ps1      # Windows PowerShell
# source .venv/bin/activate       # Linux/macOS
pip install -r requirements.txt
```

### Arranque rápido (Windows)

Para não ter de ativar o ambiente e escrever o comando à mão, há um script que
faz tudo: cria o `.venv` se faltar, instala/atualiza as dependências (quando o
`requirements.txt` muda) e arranca o serviço.

```powershell
# Arranca o serviço (equivale a: python desk_companion.py run)
.\start.ps1

# Duplo-clique no Explorador também funciona:
#   start.bat

# Qualquer argumento é passado ao desk_companion.py:
.\start.ps1 pair
.\start.ps1 run --icon-size 96 --retry 15
```

> `start.bat` é só um atalho para arranque por duplo-clique que chama o
> `start.ps1`. Ctrl+C termina o serviço.

---

## 2. Utilização

O dispositivo tem de estar ligado e a anunciar como `Desk Companion`.

```powershell
# Listar dispositivos BLE visíveis (confirma que o "Desk Companion" aparece)
python desk_companion.py scan

# Escolher e GUARDAR o dispositivo (depois é usado automaticamente)
python desk_companion.py pair

# Acertar a hora (envia a hora local do PC)
python desk_companion.py time

# Enviar uma mensagem (fica no ecrã até se tocar no display)
python desk_companion.py msg "Ola mundo"

# Demo: acerta a hora e envia uma mensagem de teste
python desk_companion.py demo

# SERVIÇO: fica a correr, religa a cada 30 s se cair, e reencaminha as
# notificações do Windows (título da app + texto + ícone). Ctrl+C para sair.
python desk_companion.py run

# Esquecer o dispositivo guardado
python desk_companion.py forget
```

### Modo serviço (`run`)

- Liga ao dispositivo **guardado** (via `pair`); se não houver, procura por nome,
  liga e **guarda-o**.
- Acerta a hora ao ligar.
- Fica a escutar as notificações do Windows e envia cada nova para o dispositivo.
- Se a ligação cair, tenta religar **a cada 30 s** até voltar (configurável com
  `--retry`). Tamanho do ícone com `--icon-size` (px, máx. 96).

### Opções globais

| Opção        | Descrição                                            |
|--------------|------------------------------------------------------|
| `--name`     | Nome a procurar (default `Desk Companion`).          |
| `--address`  | Endereço BLE direto (salta o scan; mais rápido).     |
| `--timeout`  | Timeout do scan em segundos (default 10).            |

Exemplo com endereço direto (obtido via `scan`):

```powershell
python desk_companion.py --address AA:BB:CC:DD:EE:FF msg "Direto"
```

---

## 3. Estrutura

```text
pc_app/
  desk_companion.py   -> CLI (scan/pair/forget/time/msg/demo/run)
  service.py          -> serviço persistente (auto-reconnect + notificações)
  ble_client.py       -> wrapper BLE (bleak) com os comandos do dispositivo
  win_notifications.py-> escuta de notificações do Windows + ícone (PyWinRT/Pillow)
  protocol.py         -> UUIDs, framing e helpers (partilhado)
  config.py           -> guarda/lê o dispositivo escolhido (device.json)
  test_notify.py      -> teste end-to-end BLE/visual (sem Windows; gera o ícone)
  requirements.txt    -> dependências Python
  README.md           -> este ficheiro
```

### Teste end-to-end (sem depender do Windows)

Valida o caminho BLE + visual no dispositivo: hora, mensagem, notificação só-texto
e notificação com um **ícone de quadrantes** (cima E=vermelho, D=verde; baixo
E=azul, D=branco) — ideal para confirmar cores e orientação do RGB565.

```powershell
python test_notify.py            # passo-a-passo (Enter entre cada teste)
python test_notify.py --delay 4  # automático, 4 s entre passos
```

Só precisa de `bleak` (não usa `winrt`/`Pillow`). Se as cores saírem trocadas,
ver a nota do RGB565 no firmware (`LV_COLOR_FORMAT_RGB565` vs `_SWAPPED`).

---

## 4. Funcionalidades implementadas

> Manter esta secção atualizada a cada nova funcionalidade.

| Funcionalidade            | Estado | Notas                                          |
|---------------------------|:------:|------------------------------------------------|
| Scan / descoberta         |   ✅   | `scan`                                         |
| Emparelhar/guardar device |   ✅   | `pair` / `forget` (`device.json`)              |
| Acertar hora              |   ✅   | `time` — `uint32` LE epoch local               |
| Enviar mensagem texto     |   ✅   | `msg` — fica até tocar no ecrã                 |
| Serviço auto-reconnect    |   ✅   | `run` — religa a cada 30 s                     |
| Notificações do Windows   |   ✅   | `run` — título + texto + ícone (RGB565 64×64)  |
| Enviar imagem genérica    |   ⬜   | Por implementar                                |
| Receber estado/bateria    |   ⬜   | Por implementar (BLE notify)                   |
| GUI                       |   ⬜   | Por agora só CLI                               |

---

## 5. Notas / limitações

- Sem segurança/emparelhamento BLE: ligação aberta.
- Comunicação só PC → dispositivo (sem notify do dispositivo ainda).
- A hora não persiste no dispositivo entre reboots (sem RTC/NTP) — o `run`
  reenvia a hora a cada religação.
- Ícone enviado como **RGB565 pré-renderizado** (sem PNG no dispositivo), máx 96×96.
- O reencaminhamento de notificações faz *polling* (~2 s) à lista de toasts do
  Windows; só processa notificações **novas** após o arranque.

---

## 6. Changelog

- **2026-05 — base:** CLI com `scan`, `time`, `msg`, `demo`.
- **2026-05 — serviço + notificações:** `pair`/`forget`/`run`; auto-reconnect
  (30 s); reencaminhamento das notificações do Windows (título + texto + ícone).
