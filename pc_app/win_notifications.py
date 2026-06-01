"""Escuta de notificações do Windows (UserNotificationListener) + extração de ícone.

Requer os pacotes PyWinRT (winrt-*) e Pillow. Se não estiverem disponíveis,
`WINRT_AVAILABLE` fica False e o serviço continua a funcionar (sem notificações).
"""
from __future__ import annotations

import io
import re
from dataclasses import dataclass
from pathlib import Path

import protocol as proto

# Ícone usado quando a notificação não traz logótipo da app.
DEFAULT_ICON_PATH = Path(__file__).with_name("notification.png")
# Ícones por app (mapeados pelo nome): pc_app/icons/<nome>.png
ICONS_DIR = Path(__file__).with_name("icons")


def _slug(text: str) -> str:
    """Reduz a um identificador comparável: minúsculas, só alfanuméricos."""
    return re.sub(r"[^a-z0-9]+", "", (text or "").lower())

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
        self._default_icon: tuple[bytes, int, int] | None = None
        self._default_loaded = False
        self._icon_map: dict[str, Path] | None = None    # slug -> ficheiro PNG
        self._custom_cache: dict[Path, tuple[bytes, int, int]] = {}

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

        # Prioridade: ícone mapeado por nome (icons/<app>.png) > logótipo do SO >
        # ícone por omissão (notification.png).
        icon_rgb565, w, h = self._custom_icon(app_name, title)
        if icon_rgb565 is None:
            icon_rgb565, w, h = await self._extract_icon(user_notification)
        if icon_rgb565 is None:
            icon_rgb565, w, h = self._default_icon_rgb565()
        return Notification(app_name, title, body, icon_rgb565, w, h)

    def _build_icon_map(self) -> dict[str, Path]:
        """Indexa pc_app/icons/*.png por slug do nome do ficheiro."""
        m: dict[str, Path] = {}
        if ICONS_DIR.is_dir():
            for p in ICONS_DIR.glob("*.png"):
                slug = _slug(p.stem)
                if slug:
                    m.setdefault(slug, p)
        return m

    def _custom_icon(self, app_name: str, title: str) -> tuple[bytes | None, int, int]:
        """Procura um ícone mapeado cujo slug apareça no nome da app ou no título.

        Ex.: icons/Instagram.png cobre app/título que contenham 'instagram'
        (útil para notificações reencaminhadas, onde a app emissora é sempre a
        mesma mas o título traz a app de origem).
        """
        if not PIL_AVAILABLE:
            return None, 0, 0
        if self._icon_map is None:
            self._icon_map = self._build_icon_map()
        if not self._icon_map:
            return None, 0, 0

        a, t = _slug(app_name), _slug(title)
        # Slugs mais longos primeiro: evita que um nome curto "engula" outro.
        for slug in sorted(self._icon_map, key=len, reverse=True):
            if slug in a or slug in t:
                return self._render_custom(self._icon_map[slug])
        return None, 0, 0

    def _render_custom(self, path: Path) -> tuple[bytes | None, int, int]:
        cached = self._custom_cache.get(path)
        if cached is not None:
            return cached
        try:
            with Image.open(path) as img:
                rgb565 = self._render_rgb565(img, self.icon_size)
            result = (rgb565, self.icon_size, self.icon_size)
        except Exception as e:  # pragma: no cover - depende do ficheiro
            print(f"Falha a carregar icone {path.name} ({e})")
            result = (None, 0, 0)
        self._custom_cache[path] = result
        return result

    def _default_icon_rgb565(self) -> tuple[bytes | None, int, int]:
        """Carrega e converte o notification.png uma vez (cache)."""
        if self._default_loaded:
            return self._default_icon if self._default_icon else (None, 0, 0)
        self._default_loaded = True
        if not PIL_AVAILABLE or not DEFAULT_ICON_PATH.exists():
            if not DEFAULT_ICON_PATH.exists():
                print(f"Icone por omissao nao encontrado: {DEFAULT_ICON_PATH}")
            return None, 0, 0
        try:
            with Image.open(DEFAULT_ICON_PATH) as img:
                rgb565 = self._render_rgb565(img, self.icon_size)
            self._default_icon = (rgb565, self.icon_size, self.icon_size)
            return self._default_icon
        except Exception as e:  # pragma: no cover - depende do ficheiro
            print(f"Falha a carregar icone por omissao ({e})")
            return None, 0, 0

    @staticmethod
    def _render_rgb565(img, size: int) -> bytes:
        """RGBA/-> quadrado `size`, composto sobre preto -> RGB565 LE."""
        img = img.convert("RGBA").resize((size, size))
        bg = Image.new("RGBA", img.size, (0, 0, 0, 255))
        img = Image.alpha_composite(bg, img).convert("RGB")
        return proto.rgb_to_rgb565_le(img.tobytes())

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

            with Image.open(io.BytesIO(bytes(data))) as img:
                rgb565 = self._render_rgb565(img, size)
            return rgb565, size, size
        except Exception as e:
            print(f"Sem icone da app ({e}); usa o icone por omissao")
            return None, 0, 0
