/* Fontes Chakra Petch geradas com lv_font_conv (tema "minimal tech").
 *
 * Geração (a partir de main/fonts/_ttf/, bpp 4, sem compressão):
 *   lv_font_conv --font ChakraPetch-SemiBold.ttf --size 64 --bpp 4 --no-compress \
 *       --format lvgl --symbols "0123456789: " -o font_chakra_64.c
 *   lv_font_conv --font ChakraPetch-SemiBold.ttf --size 30 --bpp 4 --no-compress \
 *       --format lvgl --symbols "0123456789"   -o font_chakra_sec_30.c
 *   lv_font_conv --font ChakraPetch-SemiBold.ttf --size 30 --bpp 4 --no-compress \
 *       --format lvgl --range 0x20-0x7F --range 0xA0-0xFF -o font_chakra_title_30.c
 *   lv_font_conv --font ChakraPetch-Regular.ttf  --size 24 --bpp 4 --no-compress \
 *       --format lvgl --range 0x20-0x7F --range 0xA0-0xFF -o font_chakra_body_24.c
 *   lv_font_conv --font ChakraPetch-SemiBold.ttf --size 18 --bpp 4 --no-compress \
 *       --format lvgl --range 0x20-0x7F --range 0xA0-0xFF -o font_chakra_18.c
 *
 * Os ranges 0x20-0x7F + 0xA0-0xFF cobrem ASCII + Latin-1 (acentos PT: à á â ã ç
 * é ê í ó ô õ ú ü e maiúsculas). As fontes de dígitos são subconjuntos (poupar
 * flash) e só servem para o relógio/contador.
 */
#pragma once

#include "lvgl.h"

LV_FONT_DECLARE(font_chakra_64)        /* HH:MM grande + contador PWR ("0-9:") */
LV_FONT_DECLARE(font_chakra_sec_30)    /* segundos do relógio ("0-9") */
LV_FONT_DECLARE(font_chakra_title_30)  /* título / boas-vindas (Latin-1) */
LV_FONT_DECLARE(font_chakra_body_24)   /* corpo da notificação (Latin-1) */
LV_FONT_DECLARE(font_chakra_18)        /* cabeçalho + bateria (Latin-1) */
