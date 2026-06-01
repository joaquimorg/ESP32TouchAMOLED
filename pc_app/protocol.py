"""Definições do protocolo BLE do Desk Companion (partilhado pelos módulos PC).

Manter sincronizado com ../BLE_SERVICES.md e o firmware (main/ble_service.c,
main/main.c).
"""
from __future__ import annotations

import unicodedata
from datetime import datetime, timezone

# --- UUIDs do GATT ---------------------------------------------------------
UUID_SERVICE = "e3f10000-8a1b-4c2d-9e5f-1a2b3c4d5e6f"
UUID_TIME = "e3f10001-8a1b-4c2d-9e5f-1a2b3c4d5e6f"
UUID_MSG = "e3f10002-8a1b-4c2d-9e5f-1a2b3c4d5e6f"
UUID_NOTIFY = "e3f10003-8a1b-4c2d-9e5f-1a2b3c4d5e6f"

DEVICE_NAME = "Desk Companion"

# --- Framing da característica de notificação (UUID_NOTIFY) ----------------
OP_TEXT = 0x01        # payload: titulo \0 corpo (UTF-8)
OP_ICON_BEGIN = 0x02  # payload: w(u16 LE) h(u16 LE), RGB565
OP_ICON_DATA = 0x03   # payload: bytes RGB565 (chunk)
OP_COMMIT = 0x04      # mostra a notificacao montada
OP_CLEAR = 0x05       # limpa e volta ao relogio

# Limites (alinhados com o firmware)
TITLE_MAX = 48        # bytes UTF-8 enviados no titulo
BODY_MAX = 180        # bytes UTF-8 enviados no corpo
ICON_MAX = 96         # dimensao maxima do icone aceite pelo dispositivo
MSG_MAX_LEN = 240     # mensagem de texto simples (UUID_MSG)


def local_epoch(dt: datetime | None = None) -> int:
    """Hora local 'wall clock' como epoch que o dispositivo espera (mostra como UTC)."""
    dt = dt or datetime.now()
    return int(dt.replace(tzinfo=timezone.utc).timestamp())


def epoch_payload(dt: datetime | None = None) -> bytes:
    return local_epoch(dt).to_bytes(4, "little")


def normalize_text(text: str) -> str:
    """Normaliza o texto para o que o dispositivo consegue mostrar.

    Aplica Unicode NFKC: dobra letras "estilizadas" (Mathematical Bold/Italic,
    fullwidth, etc.) e dígitos circulados/estilizados para a forma ASCII/canónica
    (ex.: '𝗙𝗮𝗹𝘁𝗮𝗺'->'Faltam', '𝟯'->'3') e compõe acentos ('a'+til->'ã').
    Emoji e símbolos sem equivalente ficam como estão (o dispositivo desenha-os
    se tiver glifo; senão ignora-os).
    """
    if not text:
        return text
    return unicodedata.normalize("NFKC", text)


def _truncate_utf8(text: str, max_bytes: int) -> bytes:
    """Trunca a string a no máximo `max_bytes` sem partir caracteres multibyte."""
    return text.encode("utf-8")[:max_bytes].decode("utf-8", "ignore").encode("utf-8")


# --- Construtores de frames ------------------------------------------------
def frame_text(title: str, body: str) -> bytes:
    t = _truncate_utf8(normalize_text(title or ""), TITLE_MAX)
    b = _truncate_utf8(normalize_text(body or ""), BODY_MAX)
    return bytes([OP_TEXT]) + t + b"\x00" + b


def frame_icon_begin(width: int, height: int) -> bytes:
    return bytes([OP_ICON_BEGIN]) + width.to_bytes(2, "little") + height.to_bytes(2, "little")


def frame_icon_data(chunk: bytes) -> bytes:
    return bytes([OP_ICON_DATA]) + chunk


def frame_commit() -> bytes:
    return bytes([OP_COMMIT])


def frame_clear() -> bytes:
    return bytes([OP_CLEAR])


def rgb_to_rgb565_le(rgb: bytes) -> bytes:
    """Converte bytes RGB (3 por pixel) em RGB565 little-endian (2 por pixel)."""
    out = bytearray(len(rgb) // 3 * 2)
    j = 0
    for i in range(0, len(rgb) - 2, 3):
        r, g, b = rgb[i], rgb[i + 1], rgb[i + 2]
        v = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
        out[j] = v & 0xFF
        out[j + 1] = (v >> 8) & 0xFF
        j += 2
    return bytes(out)
