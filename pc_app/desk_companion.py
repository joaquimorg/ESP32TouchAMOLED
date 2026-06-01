#!/usr/bin/env python3
"""Cliente BLE / serviço para o Desk Companion (ESP32-S3-Touch-AMOLED-2.06).

Comandos:
  scan            lista dispositivos BLE visíveis
  pair            escolhe um dispositivo e guarda-o para uso futuro
  forget          esquece o dispositivo guardado
  time            acerta a hora (hora local do PC)
  msg "texto"     envia uma mensagem de texto simples
  demo            acerta a hora e envia uma mensagem de teste
  run             SERVIÇO: liga ao dispositivo guardado, religa a cada 30 s se
                  cair, e reencaminha as notificações do Windows (título + texto
                  + ícone da app)

Protocolo BLE: ver ../BLE_SERVICES.md (fonte de verdade).
"""
import argparse
import asyncio

import config as cfg
import protocol as proto
from ble_client import DeskCompanionClient, find_device, scan
from service import run_service


async def _resolve_target(name, address):
    """Alvo para comandos pontuais: --address > guardado > procurar por nome."""
    if address:
        return address
    saved = cfg.load_device()
    if saved:
        return saved["address"]
    dev = await find_device(name, timeout=10.0)
    if dev is None:
        raise RuntimeError(f"Dispositivo '{name}' nao encontrado (usa 'pair' ou --address)")
    return dev


# --- comandos --------------------------------------------------------------
async def cmd_scan(args):
    print(f"A procurar dispositivos BLE ({args.timeout:.0f}s)...")
    for address, dev_name in await scan(args.timeout):
        marker = "  <-- Desk Companion" if dev_name == args.name else ""
        print(f"  {address}  {dev_name or '(sem nome)'}{marker}")


async def cmd_pair(args):
    print(f"A procurar dispositivos BLE ({args.timeout:.0f}s)...")
    devices = await scan(args.timeout)
    if not devices:
        print("Nenhum dispositivo encontrado.")
        return
    for i, (address, dev_name) in enumerate(devices):
        print(f"  [{i}] {address}  {dev_name or '(sem nome)'}")
    try:
        idx = int(input("Escolhe o numero do dispositivo: ").strip())
        address, dev_name = devices[idx]
    except (ValueError, IndexError):
        print("Escolha invalida.")
        return
    cfg.save_device(address, dev_name)


async def cmd_forget(args):
    cfg.clear_device()


async def cmd_time(args):
    target = await _resolve_target(args.name, args.address)
    async with DeskCompanionClient(target) as dc:
        await dc.set_time()
        print("Hora enviada.")


async def cmd_msg(args):
    target = await _resolve_target(args.name, args.address)
    async with DeskCompanionClient(target) as dc:
        await dc.send_message(args.text)
        print(f"Mensagem enviada: {args.text!r}")


async def cmd_demo(args):
    target = await _resolve_target(args.name, args.address)
    async with DeskCompanionClient(target) as dc:
        await dc.set_time()
        await dc.send_message("Ola do PC! Toca no ecra para fechar.")
        print("Demo enviada.")


async def cmd_run(args):
    await run_service(
        name=args.name,
        address=args.address,
        icon_size=args.icon_size,
        retry=args.retry,
    )


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(description="Cliente/servico BLE do Desk Companion")
    p.add_argument("--name", default=proto.DEVICE_NAME, help="nome anunciado a procurar")
    p.add_argument("--address", help="endereco BLE direto (salta o scan/guardado)")
    p.add_argument("--timeout", type=float, default=10.0, help="timeout do scan (s)")

    sub = p.add_subparsers(dest="cmd", required=True)
    sub.add_parser("scan", help="lista dispositivos BLE visiveis")
    sub.add_parser("pair", help="escolhe e guarda um dispositivo")
    sub.add_parser("forget", help="esquece o dispositivo guardado")
    sub.add_parser("time", help="acerta a hora do dispositivo")
    pm = sub.add_parser("msg", help="envia uma mensagem de texto")
    pm.add_argument("text", help="texto a mostrar (UTF-8)")
    sub.add_parser("demo", help="acerta a hora e envia uma mensagem de teste")
    pr = sub.add_parser("run", help="servico persistente + notificacoes do Windows")
    pr.add_argument("--icon-size", type=int, default=64, help="tamanho do icone (px, max 96)")
    pr.add_argument("--retry", type=float, default=30.0, help="intervalo de religacao (s)")
    return p


def main():
    args = build_parser().parse_args()
    handlers = {
        "scan": cmd_scan,
        "pair": cmd_pair,
        "forget": cmd_forget,
        "time": cmd_time,
        "msg": cmd_msg,
        "demo": cmd_demo,
        "run": cmd_run,
    }
    try:
        asyncio.run(handlers[args.cmd](args))
    except (RuntimeError, ValueError) as e:
        print(f"Erro: {e}")
        raise SystemExit(1)
    except KeyboardInterrupt:
        print("\nTerminado.")


if __name__ == "__main__":
    main()
