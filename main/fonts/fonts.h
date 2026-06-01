/* Fontes Chakra Petch geradas com lv_font_conv (tema "minimal tech").
 *
 * Geração (a partir de main/fonts/_ttf/, bpp 4, sem compressão):
 *   lv_font_conv --font ChakraPetch-SemiBold.ttf --size 64 --bpp 4 --no-compress \
 *       --format lvgl --symbols "0123456789: " -o font_chakra_64.c
 *   lv_font_conv --font ChakraPetch-SemiBold.ttf --size 30 --bpp 4 --no-compress \
 *       --format lvgl --symbols "0123456789"   -o font_chakra_sec_30.c
 *   # Fontes de texto: Chakra (Latin) + DejaVuSans (símbolos). $SYM são os
 *   # ranges de símbolos abaixo, repetidos como vários --range após o --font DejaVu.
 *   lv_font_conv --font ChakraPetch-SemiBold.ttf --range 0x20-0x7F --range 0xA0-0xFF \
 *       --font DejaVuSans.ttf $SYM --size 30 --bpp 4 --no-compress --format lvgl \
 *       -o font_chakra_title_30.c
 *   lv_font_conv --font ChakraPetch-Regular.ttf  --range 0x20-0x7F --range 0xA0-0xFF \
 *       --font DejaVuSans.ttf $SYM --size 24 --bpp 4 --no-compress --format lvgl \
 *       -o font_chakra_body_24.c
 *   lv_font_conv --font ChakraPetch-SemiBold.ttf --range 0x20-0x7F --range 0xA0-0xFF \
 *       --font DejaVuSans.ttf $SYM --size 18 --bpp 4 --no-compress --format lvgl \
 *       -o font_chakra_18.c
 *
 *   $SYM (símbolos da DejaVu, monocromáticos):
 *     --range 0x2190-0x2194 --range 0x21D0 --range 0x21D2 --range 0x2022
 *     --range 0x25E6 --range 0x2605-0x2606 --range 0x2660-0x2666 --range 0x2713
 *     --range 0x2717 --range 0x20AC --range 0x2122 --range 0x2116 --range 0x2248
 *     --range 0x2260 --range 0x2264-0x2265
 *   (setas ← ↑ → ↓ ↔ ⇐ ⇒, bullets • ◦, estrelas ★ ☆, naipes ♠ ♣ ♥ ♦,
 *    ✓ ✗, € ™ №, ≈ ≠ ≤ ≥)
 *
 * Os ranges 0x20-0x7F + 0xA0-0xFF cobrem ASCII + Latin-1 (acentos PT: à á â ã ç
 * é ê í ó ô õ ú ü e maiúsculas). As fontes de dígitos são subconjuntos (poupar
 * flash) e só servem para o relógio/contador. Unicode fora destes conjuntos
 * (ex.: emoji) não tem glifo e, com LV_USE_FONT_PLACEHOLDER desligado, não é
 * desenhado (ver sanitize_text() em main.c).
 */
#pragma once

#include "lvgl.h"

LV_FONT_DECLARE(font_chakra_64)        /* HH:MM grande + contador PWR ("0-9:") */
LV_FONT_DECLARE(font_chakra_sec_30)    /* segundos do relógio ("0-9") */
LV_FONT_DECLARE(font_chakra_title_30)  /* título / boas-vindas (Latin-1 + símbolos) */
LV_FONT_DECLARE(font_chakra_body_24)   /* corpo da notificação (Latin-1 + símbolos) */
LV_FONT_DECLARE(font_chakra_18)        /* cabeçalho + bateria (Latin-1 + símbolos) */
