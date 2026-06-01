"""Cliente BLE (bleak) para o Desk Companion."""
from __future__ import annotations

import asyncio
from datetime import datetime

from bleak import BleakClient, BleakScanner

import protocol as proto


async def scan(timeout: float = 8.0):
    """Lista (address, name) dos dispositivos BLE visíveis."""
    devices = await BleakScanner.discover(timeout=timeout)
    return [(d.address, d.name) for d in devices]


async def find_device(name: str = proto.DEVICE_NAME, timeout: float = 10.0):
    """Procura um dispositivo pelo nome; devolve o objeto device ou None."""
    return await BleakScanner.find_device_by_name(name, timeout=timeout)


class DeskCompanionClient:
    """Wrapper assíncrono sobre o BleakClient com os comandos do Desk Companion."""

    def __init__(self, target, on_disconnect=None):
        self._target = target
        self._on_disconnect = on_disconnect
        self._client: BleakClient | None = None
        self.icon_write_batch = 8

    @property
    def is_connected(self) -> bool:
        return self._client is not None and self._client.is_connected

    @property
    def _chunk_size(self) -> int:
        """Bytes de payload BLE por escrita (deixa 1 byte para o opcode)."""
        if self._client is not None:
            try:
                svc = self._client.services.get_characteristic(proto.UUID_NOTIFY)
                max_no_rsp = getattr(svc, "max_write_without_response_size", None)
                if max_no_rsp:
                    return max(20, min(244, max_no_rsp) - 1)
            except Exception:
                pass
        mtu = getattr(self._client, "mtu_size", 23) or 23
        return max(20, min(244, mtu - 3) - 1)

    async def connect(self):
        self._client = BleakClient(self._target, disconnected_callback=self._on_disconnect)
        await self._client.connect()

    async def disconnect(self):
        if self._client is not None:
            try:
                await self._client.disconnect()
            finally:
                self._client = None

    async def __aenter__(self) -> "DeskCompanionClient":
        await self.connect()
        return self

    async def __aexit__(self, *exc):
        await self.disconnect()

    # --- comandos ----------------------------------------------------------
    async def set_time(self, dt: datetime | None = None):
        await self._client.write_gatt_char(proto.UUID_TIME, proto.epoch_payload(dt), response=True)

    async def send_message(self, text: str):
        data = proto.normalize_text(text or "").encode("utf-8")[: proto.MSG_MAX_LEN]
        await self._client.write_gatt_char(proto.UUID_MSG, data, response=True)

    async def clear(self):
        await self._client.write_gatt_char(proto.UUID_NOTIFY, proto.frame_clear(), response=True)

    async def send_notification(
        self,
        title: str,
        body: str,
        icon_rgb565: bytes | None = None,
        icon_w: int = 0,
        icon_h: int = 0,
    ):
        """Envia uma notificação rica: ícone (opcional) + título + corpo."""
        c = proto.UUID_NOTIFY

        if icon_rgb565 and icon_w and icon_h:
            await self._client.write_gatt_char(c, proto.frame_icon_begin(icon_w, icon_h), response=True)
            step = self._chunk_size
            for i in range(0, len(icon_rgb565), step):
                chunk = icon_rgb565[i : i + step]
                await self._client.write_gatt_char(c, proto.frame_icon_data(chunk), response=False)
                if i and self.icon_write_batch and (i // step) % self.icon_write_batch == 0:
                    await asyncio.sleep(0)

        await self._client.write_gatt_char(c, proto.frame_text(title, body), response=True)
        await self._client.write_gatt_char(c, proto.frame_commit(), response=True)
