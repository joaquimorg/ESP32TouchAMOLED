"""Serviço persistente: liga ao dispositivo guardado, religa a cada 30 s se cair,
e reencaminha as notificações do Windows."""
from __future__ import annotations

import asyncio

import config as cfg
import protocol as proto
from ble_client import DeskCompanionClient, find_device
from win_notifications import WinNotificationListener


async def _resolve_initial_target(name: str, address: str | None):
    """Decide o alvo inicial: --address > guardado > (será procurado no loop)."""
    if address:
        cfg.save_device(address, name)
        return address
    saved = cfg.load_device()
    if saved:
        print(f"A usar dispositivo guardado: {saved.get('name') or '?'} [{saved['address']}]")
        return saved["address"]
    return None


async def _discover_and_save(name: str, timeout: float = 10.0):
    print(f"A procurar '{name}' ...")
    dev = await find_device(name, timeout=timeout)
    if dev is None:
        return None
    cfg.save_device(dev.address, dev.name)
    return dev


async def run_service(
    name: str = proto.DEVICE_NAME,
    address: str | None = None,
    icon_size: int = 64,
    poll_interval: float = 2.0,
    retry: float = 30.0,
):
    # Preparar a escuta de notificações do Windows (opcional).
    listener: WinNotificationListener | None = WinNotificationListener(icon_size)
    if listener.available():
        if not await listener.request_access():
            listener = None
    else:
        print("Notificacoes do Windows indisponiveis (winrt/Pillow em falta).")
        print("O servico vai na mesma religar e acertar a hora.")
        listener = None

    target = await _resolve_initial_target(name, address)

    print("Servico a correr. Ctrl+C para sair.")
    while True:
        # Garantir um alvo (procura se ainda não há).
        if target is None:
            dev = await _discover_and_save(name)
            if dev is None:
                print(f"Nao encontrado. Nova tentativa em {retry:.0f}s.")
                await asyncio.sleep(retry)
                continue
            target = dev

        disconnected = asyncio.Event()

        def _on_disc(_):
            print("Ligacao perdida.")
            disconnected.set()

        client = DeskCompanionClient(target, on_disconnect=_on_disc)
        try:
            print(f"A ligar a {getattr(target, 'address', target)} ...")
            await client.connect()
            print("Ligado. A acertar a hora.")
            await client.set_time()
            await _forward_loop(client, listener, disconnected, poll_interval)
        except Exception as e:
            print(f"Erro de ligacao: {e}")
        finally:
            await client.disconnect()

        print(f"Religar em {retry:.0f}s ...")
        await asyncio.sleep(retry)


async def _forward_loop(client, listener, disconnected, poll_interval):
    """Enquanto ligado, encaminha as notificações novas do Windows."""
    while client.is_connected and not disconnected.is_set():
        if listener is not None:
            for nt in await listener.poll_new():
                try:
                    print(f"-> [{nt.app_name}] {nt.title} | {nt.body[:50]!r}")
                    await client.send_notification(
                        nt.title or nt.app_name, nt.body,
                        nt.icon_rgb565, nt.icon_w, nt.icon_h,
                    )
                except Exception as e:
                    print(f"Falha a enviar: {e}")
        await asyncio.sleep(poll_interval)
