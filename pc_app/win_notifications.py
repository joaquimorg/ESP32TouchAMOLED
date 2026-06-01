"""Escuta de notificações do Windows (UserNotificationListener) + extração de ícone.

Requer os pacotes PyWinRT (winrt-*) e Pillow. Se não estiverem disponíveis,
`WINRT_AVAILABLE` fica False e o serviço continua a funcionar (sem notificações).
"""
from __future__ import annotations

import io
from dataclasses import dataclass

import protocol as proto

WINRT_AVAILABLE = True
WINRT_IMPORT_ERROR = ""
try:
    from winrt.windows.ui.notifications.management import (
        UserNotificationListener,
        UserNotificationListenerAccessStatus,
    )
    from winrt.windows.ui.notifications import NotificationKinds, KnownNotificationBindings
    from winrt.windows.foundation import Size
    from winrt.windows.storage.streams import DataReader
except Exception as e:  # pragma: no cover - depende do ambiente
    WINRT_AVAILABLE = False
    WINRT_IMPORT_ERROR = str(e)

try:
    from PIL import Image
    PIL_AVAILABLE = True
except Exception:  # pragma: no cover
    PIL_AVAILABLE = False


@dataclass
class Notification:
    app_name: str
    title: str
    body: str
    icon_rgb565: bytes | None = None
    icon_w: int = 0
    icon_h: int = 0


class WinNotificationListener:
    """Lê novas notificações toast do Windows."""

    def __init__(self, icon_size: int = 64):
        self.icon_size = min(icon_size, proto.ICON_MAX)
        self._listener = None
        self._seen: set[int] = set()
        self._primed = False

    @staticmethod
    def available() -> bool:
        return WINRT_AVAILABLE

    async def request_access(self) -> bool:
        if not WINRT_AVAILABLE:
            print(f"WinRT indisponivel: {WINRT_IMPORT_ERROR}")
            return False
        self._listener = UserNotificationListener.current
        status = await self._listener.request_access_async()
        if status != UserNotificationListenerAccessStatus.ALLOWED:
            print(
                "Acesso as notificacoes NEGADO. Ativar em: Definicoes > Privacidade "
                "e seguranca > Notificacoes (acesso de apps)."
            )
            return False
        return True

    async def poll_new(self) -> list[Notification]:
        """Devolve as notificações novas desde a última chamada."""
        if self._listener is None:
            return []
        try:
            current = await self._listener.get_notifications_async(NotificationKinds.TOAST)
        except Exception as e:
            print(f"Erro a ler notificacoes: {e}")
            return []

        items = list(current)
        ids_now = {n.id for n in items}

        # Na primeira passagem só memoriza as existentes (não reenvia o que já lá estava).
        if not self._primed:
            self._seen = ids_now
            self._primed = True
            return []

        new = [n for n in items if n.id not in self._seen]
        self._seen = ids_now

        out: list[Notification] = []
        for n in new:
            try:
                out.append(await self._convert(n))
            except Exception as e:
                print(f"Erro a converter notificacao: {e}")
        return out

    async def _convert(self, user_notification) -> Notification:
        app_name = ""
        try:
            app_name = user_notification.app_info.display_info.display_name or ""
        except Exception:
            pass

        title, body = self._extract_text(user_notification)
        if not title:
            title = app_name

        icon_rgb565, w, h = await self._extract_icon(user_notification)
        return Notification(app_name, title, body, icon_rgb565, w, h)

    @staticmethod
    def _extract_text(user_notification):
        try:
            binding = user_notification.notification.visual.get_binding(
                KnownNotificationBindings.toast_generic
            )
            if binding is None:
                return "", ""
            texts = [t.text for t in binding.get_text_elements()]
            texts = [t for t in texts if t]
            if not texts:
                return "", ""
            return texts[0], "\n".join(texts[1:])
        except Exception:
            return "", ""

    async def _extract_icon(self, user_notification):
        if not PIL_AVAILABLE:
            return None, 0, 0
        try:
            size = self.icon_size
            display_info = user_notification.app_info.display_info
            ref = display_info.get_logo(Size(float(size), float(size)))
            if ref is None:
                return None, 0, 0
            stream = await ref.open_read_async()
            n = stream.size
            if not n:
                return None, 0, 0
            reader = DataReader(stream)
            await reader.load_async(n)
            data = bytearray(n)
            reader.read_bytes(data)

            img = Image.open(io.BytesIO(bytes(data))).convert("RGBA").resize((size, size))
            bg = Image.new("RGBA", img.size, (0, 0, 0, 255))
            img = Image.alpha_composite(bg, img).convert("RGB")
            rgb565 = proto.rgb_to_rgb565_le(img.tobytes())
            return rgb565, size, size
        except Exception as e:
            print(f"Sem icone ({e})")
            return None, 0, 0
