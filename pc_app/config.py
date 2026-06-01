"""Persistência do dispositivo escolhido (para religar automaticamente)."""
from __future__ import annotations

import json
import os

CONFIG_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "device.json")


def load_device() -> dict | None:
    """Devolve {'address':..., 'name':...} guardado, ou None."""
    try:
        with open(CONFIG_PATH, "r", encoding="utf-8") as f:
            data = json.load(f)
        if isinstance(data, dict) and data.get("address"):
            return data
    except (FileNotFoundError, json.JSONDecodeError):
        pass
    return None


def save_device(address: str, name: str | None) -> None:
    with open(CONFIG_PATH, "w", encoding="utf-8") as f:
        json.dump({"address": address, "name": name}, f, indent=2)
    print(f"Dispositivo guardado em {CONFIG_PATH}: {name or '?'} [{address}]")


def clear_device() -> None:
    try:
        os.remove(CONFIG_PATH)
        print("Dispositivo guardado removido.")
    except FileNotFoundError:
        pass
