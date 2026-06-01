#!/usr/bin/env python3
"""Teste end-to-end do caminho BLE/visual do Desk Companion.

NÃO depende das notificações do Windows nem do Pillow — gera o ícone localmente.
Serve para validar: acerto de hora, mensagem simples, notificação só-texto e
notificação com ícone (cores + orientação).

O ícone de teste tem 4 quadrantes:
    cima-esquerda = VERMELHO   cima-direita  = VERDE
    baixo-esquerda = AZUL      baixo-direita = BRANCO
Assim vê-se logo se as cores estão trocadas (RGB565 swapped) ou a imagem espelhada.

Uso:
  python test_notify.py                 # usa o dispositivo guardado (pair) ou procura
  python test_notify.py --address AA:.. # endereço direto
  python test_notify.py --delay 4       # corre tudo com 4 s entre passos (sem Enter)
  python test_notify.py --icon-size 64
"""
import argparse
import asyncio

import config as cfg
import protocol as proto
from ble_client import DeskCompanionClient, find_device


async def resolve_target(name, address):
    if address:
        return address
    saved = cfg.load_device()
    if saved:
        return saved["address"]
    dev = await find_device(name, timeout=10.0)
    if dev is None:
        raise RuntimeError(f"Dispositivo '{name}' nao encontrado (usa 'pair' ou --address)")
    return dev


def make_quadrant_icon_rgb565(size: int) -> bytes:
    """Gera um ícone de 4 quadrantes (R/G/B/branco) em RGB565 LE."""
    half = size // 2
    rgb = bytearray(size * size * 3)
    RED, GREEN, BLUE, WHITE = (255, 0, 0), (0, 255, 0), (0, 0, 255), (255, 255, 255)
    for y in range(size):
        for x in range(size):
            if y < half:
                color = RED if x < half else GREEN
            else:
                color = BLUE if x < half else WHITE
            i = (y * size + x) * 3
            rgb[i], rgb[i + 1], rgb[i + 2] = color
    return proto.rgb_to_rgb565_le(bytes(rgb))


async def pause(args, msg: str):
    if args.delay is not None:
        print(f"{msg}  (a aguardar {args.delay:.0f}s)")
        await asyncio.sleep(args.delay)
    else:
        loop = asyncio.get_event_loop()
        await loop.run_in_executor(None, input, f"{msg}  [Enter para continuar] ")


async def main_async(args):
    target = await resolve_target(args.name, args.address)
    print(f"A ligar a {getattr(target, 'address', target)} ...")
    async with DeskCompanionClient(target) as dc:
        print("Ligado.\n")

        print("[1/5] Acertar a hora -> o relogio deve mostrar a hora certa.")
        await dc.set_time()
        await pause(args, "Confirma o relogio.")

        print("[2/5] Mensagem simples (UUID_MSG).")
        await dc.send_message("Teste de mensagem simples. Toca no ecra para fechar.")
        await pause(args, "Deve aparecer o texto. Toca no ecra para voltar ao relogio.")

        print("[3/5] Notificacao SO TEXTO (titulo + corpo).")
        await dc.clear()              # garante relogio limpo antes (sem residuo)
        await asyncio.sleep(0.4)
        await dc.send_notification("App Teste", "Notificacao so com texto.\nSegunda linha.")
        await pause(args, "Deve aparecer titulo + corpo. Toca no ecra.")

        print("[4/5] Notificacao COM ICONE (quadrantes R/G/B/branco).")
        await dc.clear()              # garante relogio limpo antes (sem residuo)
        await asyncio.sleep(0.4)
        icon = make_quadrant_icon_rgb565(args.icon_size)
        await dc.send_notification(
            "App Teste",
            "Icone: cima E=vermelho, D=verde; baixo E=azul, D=branco.",
            icon, args.icon_size, args.icon_size,
        )
        await pause(args, "Confirma cores/orientacao do icone. Toca no ecra.")

        print("[5/5] CLEAR remoto -> volta ao relogio sem tocar.")
        await dc.clear()
        print("\nTeste concluido.")


def main():
    p = argparse.ArgumentParser(description="Teste BLE/visual do Desk Companion (sem Windows)")
    p.add_argument("--name", default=proto.DEVICE_NAME, help="nome a procurar")
    p.add_argument("--address", help="endereco BLE direto")
    p.add_argument("--icon-size", type=int, default=64, help="tamanho do icone (px, max 96)")
    p.add_argument("--delay", type=float, default=None,
                   help="segundos entre passos (sem Enter manual)")
    args = p.parse_args()
    if args.icon_size > proto.ICON_MAX:
        args.icon_size = proto.ICON_MAX
    try:
        asyncio.run(main_async(args))
    except (RuntimeError, ValueError) as e:
        print(f"Erro: {e}")
        raise SystemExit(1)
    except KeyboardInterrupt:
        print("\nInterrompido.")


if __name__ == "__main__":
    main()
