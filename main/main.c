#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <time.h>
#include <sys/time.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_heap_caps.h"
#include "nvs_flash.h"
#include "lvgl.h"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "power_button.h"
#include "ble_service.h"
#include "fonts/fonts.h"

/* Protocolo de framing da característica de notificação (ver BLE_SERVICES.md). */
#define OP_TEXT        0x01   /* payload: titulo \0 corpo (UTF-8) */
#define OP_ICON_BEGIN  0x02   /* payload: w(u16 LE) h(u16 LE), formato RGB565 */
#define OP_ICON_DATA   0x03   /* payload: bytes RGB565 (chunk) */
#define OP_COMMIT      0x04   /* mostra a notificacao montada */
#define OP_CLEAR       0x05   /* limpa e volta ao relogio */

#define NOTIF_TITLE_MAX  64
#define NOTIF_BODY_MAX   200
#define NOTIF_ICON_MAX_W 96
#define NOTIF_ICON_MAX_H 96
#define NOTIF_QUEUE_LEN  4

#define BLE_DEVICE_NAME       "Desk Companion"
#define POWEROFF_COUNT_FROM   3       /* contagem mostrada enquanto o botão PWR está premido */
#define REMOTE_WAIT_MS        30000   /* espera por ligação remota antes do modo relógio */
#define CLOCK_MOVE_PERIOD_MS  30000   /* de quanto em quanto tempo o relógio muda de sítio */
#define CLOCK_MOVE_ANIM_MS    1200    /* duração da animação de movimento */
#define CLOCK_BRIGHTNESS      50
#define ACTIVE_BRIGHTNESS     100
#define SAFE_MARGIN_X         36      /* evita cantos arredondados do AMOLED */
#define SAFE_MARGIN_BOTTOM    28

/* --- Paleta minimal tech (fundo preto AMOLED, acento único e discreto) --- */
#define CLR_ACCENT     0x35C9DE      /* cyan-teal suave (acento) */
#define CLR_TEXT       0xF2F4F5      /* branco "quente" para texto principal */
#define CLR_TEXT_DIM   0x9AA0A6      /* cinza para texto secundário */
#define CLR_HAIRLINE   0x303538      /* linhas/molduras muito discretas */

static const char *TAG = "desk_companion";

/* --- UI base --- */
static lv_obj_t *s_welcome_label;

/* --- Overlay do contador do botão PWR (painel preto full-screen) --- */
static lv_obj_t *s_countdown_panel;
static lv_obj_t *s_countdown_label;

/* --- Ecrã principal e painel de notificação full-screen. --- */
static lv_obj_t *s_main_screen;    /* relógio / boas-vindas */
static lv_obj_t *s_notif_screen;   /* painel de notificação por cima do ecrã principal */

/* --- Conteúdo da notificação --- */
static lv_obj_t *s_notif_card;     /* cartão central (filho de s_notif_screen) */
static lv_obj_t *s_notif_header;   /* cabeçalho em maiúsculas espaçadas */
static lv_obj_t *s_msg_row;        /* linha: coluna do ícone + coluna do título */
static lv_obj_t *s_msg_icon_box;   /* coluna do ícone (glow subtil, sem borda) */
static lv_obj_t *s_msg_icon;
static lv_obj_t *s_msg_title;
static lv_obj_t *s_msg_divider;
static lv_obj_t *s_msg_body;
static lv_obj_t *s_msg_dots;
static lv_obj_t *s_msg_dot[5];

/* --- Estado de montagem da notificação recebida por BLE --- */
static char            s_n_title[NOTIF_TITLE_MAX];
static char            s_n_body[NOTIF_BODY_MAX];
static uint8_t        *s_icon_buf;     /* buffer RGB565 (PSRAM) */
static uint32_t        s_icon_cap;     /* bytes alocados */
static uint32_t        s_icon_len;     /* bytes recebidos */
static uint16_t        s_icon_w, s_icon_h;
static uint8_t        *s_display_icon_buf;
static uint32_t        s_display_icon_cap;
static lv_image_dsc_t  s_icon_dsc;

typedef struct {
    char title[NOTIF_TITLE_MAX];
    char body[NOTIF_BODY_MAX];
    uint8_t *icon_buf;
    uint32_t icon_len;
    uint16_t icon_w;
    uint16_t icon_h;
    bool has_icon;
} notif_item_t;

static notif_item_t s_notif_queue[NOTIF_QUEUE_LEN];
static uint8_t s_notif_q_head;
static uint8_t s_notif_q_count;

/* --- Modo relógio --- */
static lv_obj_t  *s_clock_label;     /* HH:MM */
static lv_obj_t  *s_clock_seconds;   /* SS destacado (acento) */
static lv_obj_t  *s_clock_line;      /* linha fina de acento */
static lv_obj_t  *s_clock_group;
static lv_obj_t  *s_batt_group;
static lv_obj_t  *s_batt_body;
static lv_obj_t  *s_batt_fill;
static lv_obj_t  *s_batt_tip;
static lv_obj_t  *s_batt_label;
static lv_timer_t *s_clock_tick_timer;
static lv_timer_t *s_clock_move_timer;
static lv_timer_t *s_power_status_timer;

/* --- Espera por ligação remota --- */
static lv_timer_t *s_wait_timer;
static uint32_t    s_wait_start_ms;

static void enter_clock_mode(void);
static void clock_update_time(void);
static void show_notification_ui(bool has_icon);
static void hide_notification_ui(void);
static void power_status_update(void);
static void create_battery_indicator(lv_obj_t *parent);
static void update_notification_dots(void);
static void sanitize_text(char *text);
static bool notification_is_visible(void);
static void show_next_queued_notification(void);

static void schedule_lvgl(lv_async_cb_t cb, void *user_data)
{
    bsp_display_lock(0);
    lv_async_call(cb, user_data);
    bsp_display_unlock();
}

/* Limpa o texto recebido por BLE preservando UTF-8 válido:
 *  - ASCII (0x20-0x7F) e Latin-1 (acentos PT) passam intactos;
 *  - pontuação tipográfica (U+2000..U+206F: travessões, aspas curvas,
 *    reticências) é convertida para o equivalente ASCII (não há glifo);
 *  - restantes sequências UTF-8 válidas (setas, bullets, €, ™, ≥, ★, ...) são
 *    PRESERVADAS — aparecem se a fonte tiver o glifo; senão (ex.: emoji), com o
 *    placeholder do LVGL desligado, simplesmente não são desenhadas;
 *  - bytes inválidos são descartados.
 * Escreve sempre <= bytes lidos, por isso opera in-place em segurança. */
static void sanitize_text(char *text)
{
    char *r = text;
    char *w = text;

    while (*r) {
        uint8_t c = (uint8_t)*r;
        if (c < 0x80) {
            *w++ = *r++;
            continue;
        }

        /* Pontuação em U+2000..U+206F (0xE2 0x80 0xXX) -> ASCII. */
        if (c == 0xE2 && (uint8_t)r[1] == 0x80 && r[2]) {
            const char *repl = NULL;
            switch ((uint8_t)r[2]) {
            case 0x93: case 0x94: repl = "-";   break;  /* – — */
            case 0x98: case 0x99: repl = "'";   break;  /* ‘ ’ */
            case 0x9C: case 0x9D: repl = "\"";  break;  /* “ ” */
            case 0xA6:            repl = "..."; break;  /* … */
            default: break;
            }
            if (repl) {
                while (*repl) {
                    *w++ = *repl++;
                }
                r += 3;
                continue;
            }
        }

        /* Sequência UTF-8 válida (Latin-1, símbolos, etc.): preserva os bytes.
         * Valida os bytes de continuação; se inválida, descarta 1 byte. */
        int step = (c & 0xE0) == 0xC0 ? 2 : ((c & 0xF0) == 0xE0 ? 3 : ((c & 0xF8) == 0xF0 ? 4 : 0));
        bool valid = (step >= 2);
        for (int k = 1; valid && k < step; k++) {
            if (((uint8_t)r[k] & 0xC0) != 0x80) {
                valid = false;
            }
        }
        if (valid) {
            for (int k = 0; k < step; k++) {
                *w++ = *r++;
            }
        } else {
            r++;
        }
    }
    *w = '\0';
}

/* Garante que o modo relógio está ativo: cria-o se ainda não existir, ou
 * retoma-o (mostra + retoma timers) se estava pausado por uma notificação. */
static void ensure_clock_mode(void)
{
    if (s_clock_label == NULL) {
        if (s_wait_timer) {
            lv_timer_delete(s_wait_timer);
            s_wait_timer = NULL;
        }
        enter_clock_mode();
    } else {
        lv_obj_remove_flag(s_clock_group, LV_OBJ_FLAG_HIDDEN);
        if (s_clock_tick_timer) {
            lv_timer_resume(s_clock_tick_timer);
        }
        if (s_clock_move_timer) {
            lv_timer_resume(s_clock_move_timer);
        }
        if (s_power_status_timer) {
            lv_timer_resume(s_power_status_timer);
        }
        bsp_display_brightness_set(CLOCK_BRIGHTNESS);
        power_status_update();
        clock_update_time();
    }
}

/* ============================================================================
 * Modo relógio
 * ==========================================================================*/
static void clock_update_time(void)
{
    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    lv_label_set_text_fmt(s_clock_label, "%02d:%02d", tm_now.tm_hour, tm_now.tm_min);
    if (s_clock_seconds) {
        lv_label_set_text_fmt(s_clock_seconds, "%02d", tm_now.tm_sec);
        /* Reposiciona o "superscript" caso a largura de HH:MM mude (ex.: 9->10). */
        lv_obj_align_to(s_clock_seconds, s_clock_label, LV_ALIGN_OUT_RIGHT_TOP, 6, 6);
    }
}

static void clock_tick_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);
    clock_update_time();
}

static void power_status_update(void)
{
    if (!s_batt_label || !s_batt_body || !s_batt_fill || !s_batt_tip) {
        return;
    }

    power_status_t st = {0};
    if (power_status_read(&st) != ESP_OK) {
        lv_label_set_text(s_batt_label, "--");
        lv_obj_set_width(s_batt_fill, 0);
        return;
    }

    int percent = st.battery_percent >= 0 ? st.battery_percent : 0;
    lv_obj_set_width(s_batt_fill, (46 * percent) / 100);
    lv_color_t fill_color = lv_color_hex(0x21D07A);
    if (percent <= 15 && !st.vbus_present) {
        fill_color = lv_color_hex(0xE5484D);
    } else if (percent <= 35 && !st.vbus_present) {
        fill_color = lv_color_hex(0xF7B955);
    }
    lv_obj_set_style_bg_color(s_batt_fill, fill_color, 0);
    lv_obj_set_style_bg_color(s_batt_tip, lv_color_hex(0xD8D8D8), 0);

    if (st.vbus_present && st.charging && st.battery_percent >= 0) {
        lv_label_set_text_fmt(s_batt_label, "CHG %d%%", st.battery_percent);
    } else if (st.vbus_present) {
        lv_label_set_text(s_batt_label, "USB-C");
    } else if (st.battery_percent >= 0) {
        lv_label_set_text_fmt(s_batt_label, "%d%%", st.battery_percent);
    } else {
        lv_label_set_text(s_batt_label, "--");
    }
}

static void power_status_timer_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);
    power_status_update();
}

static void create_battery_indicator(lv_obj_t *parent)
{
    s_batt_group = lv_obj_create(parent);
    lv_obj_set_size(s_batt_group, 152, 26);
    lv_obj_align(s_batt_group, LV_ALIGN_BOTTOM_MID, 0, -2);
    lv_obj_set_style_bg_opa(s_batt_group, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_batt_group, 0, 0);
    lv_obj_set_style_pad_all(s_batt_group, 0, 0);
    lv_obj_remove_flag(s_batt_group, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(s_batt_group, LV_OBJ_FLAG_CLICKABLE);

    s_batt_body = lv_obj_create(s_batt_group);
    lv_obj_set_size(s_batt_body, 54, 22);
    lv_obj_align(s_batt_body, LV_ALIGN_RIGHT_MID, -6, 0);
    lv_obj_set_style_bg_opa(s_batt_body, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(s_batt_body, lv_color_hex(0x6A6F73), 0);
    lv_obj_set_style_border_width(s_batt_body, 2, 0);
    lv_obj_set_style_radius(s_batt_body, 4, 0);
    lv_obj_set_style_pad_all(s_batt_body, 2, 0);
    lv_obj_remove_flag(s_batt_body, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(s_batt_body, LV_OBJ_FLAG_CLICKABLE);

    s_batt_fill = lv_obj_create(s_batt_body);
    lv_obj_set_size(s_batt_fill, 0, 14);
    lv_obj_align(s_batt_fill, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(s_batt_fill, lv_color_hex(0x21D07A), 0);
    lv_obj_set_style_bg_opa(s_batt_fill, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_batt_fill, 0, 0);
    lv_obj_set_style_radius(s_batt_fill, 2, 0);
    lv_obj_remove_flag(s_batt_fill, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(s_batt_fill, LV_OBJ_FLAG_CLICKABLE);

    s_batt_tip = lv_obj_create(s_batt_group);
    lv_obj_set_size(s_batt_tip, 6, 12);
    lv_obj_align_to(s_batt_tip, s_batt_body, LV_ALIGN_OUT_RIGHT_MID, 1, 0);
    lv_obj_set_style_bg_color(s_batt_tip, lv_color_hex(0x6A6F73), 0);
    lv_obj_set_style_bg_opa(s_batt_tip, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_batt_tip, 0, 0);
    lv_obj_set_style_radius(s_batt_tip, 2, 0);
    lv_obj_remove_flag(s_batt_tip, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(s_batt_tip, LV_OBJ_FLAG_CLICKABLE);

    s_batt_label = lv_label_create(s_batt_group);
    lv_label_set_text(s_batt_label, "--");
    lv_obj_set_style_text_color(s_batt_label, lv_color_hex(CLR_TEXT_DIM), 0);
    lv_obj_set_style_text_font(s_batt_label, &font_chakra_18, 0);
    lv_label_set_long_mode(s_batt_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(s_batt_label, 90);
    lv_obj_align(s_batt_label, LV_ALIGN_LEFT_MID, 0, 0);
}

static void anim_x_cb(void *var, int32_t v)
{
    lv_obj_set_x((lv_obj_t *)var, v);
}

static void anim_y_cb(void *var, int32_t v)
{
    lv_obj_set_y((lv_obj_t *)var, v);
}

/* Move o relógio para uma posição aleatória dentro do ecrã, com animação suave
 * (anti burn-in AMOLED). */
static void clock_move_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);

    lv_obj_update_layout(s_clock_group);

    int32_t scr_w = lv_obj_get_width(s_main_screen);
    int32_t scr_h = lv_obj_get_height(s_main_screen);
    int32_t obj_w = lv_obj_get_width(s_clock_group);
    int32_t obj_h = lv_obj_get_height(s_clock_group);

    int32_t min_x = SAFE_MARGIN_X;
    int32_t min_y = SAFE_MARGIN_X;
    int32_t max_x = scr_w - obj_w - SAFE_MARGIN_X;
    int32_t max_y = scr_h - obj_h - SAFE_MARGIN_BOTTOM;
    if (max_x < 0) max_x = 0;
    if (max_y < 0) max_y = 0;
    if (max_x < min_x) min_x = max_x;
    if (max_y < min_y) min_y = max_y;

    int32_t nx = min_x + (int32_t)(esp_random() % (uint32_t)(max_x - min_x + 1));
    int32_t ny = min_y + (int32_t)(esp_random() % (uint32_t)(max_y - min_y + 1));

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_clock_group);
    lv_anim_set_duration(&a, CLOCK_MOVE_ANIM_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);

    lv_anim_set_exec_cb(&a, anim_x_cb);
    lv_anim_set_values(&a, lv_obj_get_x(s_clock_group), nx);
    lv_anim_start(&a);

    lv_anim_set_exec_cb(&a, anim_y_cb);
    lv_anim_set_values(&a, lv_obj_get_y(s_clock_group), ny);
    lv_anim_start(&a);
}

static void enter_clock_mode(void)
{
    ESP_LOGI(TAG, "Sem ligacao remota apos %d ms -> modo relogio", REMOTE_WAIT_MS);
    bsp_display_brightness_set(CLOCK_BRIGHTNESS);

    lv_obj_add_flag(s_welcome_label, LV_OBJ_FLAG_HIDDEN);

    s_clock_group = lv_obj_create(s_main_screen);
    lv_obj_set_size(s_clock_group, 250, 150);
    lv_obj_set_style_bg_opa(s_clock_group, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_clock_group, 0, 0);
    lv_obj_set_style_pad_all(s_clock_group, 0, 0);
    lv_obj_remove_flag(s_clock_group, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(s_clock_group, LV_OBJ_FLAG_CLICKABLE);

    /* HH:MM grande, fonte tech nativa (Chakra Petch 64) — nítida, sem transform. */
    s_clock_label = lv_label_create(s_clock_group);
    lv_obj_set_style_text_color(s_clock_label, lv_color_hex(CLR_TEXT), 0);
    lv_obj_set_style_text_font(s_clock_label, &font_chakra_64, 0);
    lv_obj_set_style_text_letter_space(s_clock_label, 2, 0);
    lv_obj_align(s_clock_label, LV_ALIGN_TOP_MID, -8, 2);

    /* SS destacado, pequeno e em acento, tipo "superscript". */
    s_clock_seconds = lv_label_create(s_clock_group);
    lv_obj_set_style_text_color(s_clock_seconds, lv_color_hex(CLR_ACCENT), 0);
    lv_obj_set_style_text_font(s_clock_seconds, &font_chakra_sec_30, 0);
    lv_obj_set_style_text_letter_space(s_clock_seconds, 1, 0);

    /* Linha fina de acento por baixo das horas (toque "tech", discreto). */
    s_clock_line = lv_obj_create(s_clock_group);
    lv_obj_set_size(s_clock_line, 168, 3);
    lv_obj_set_style_bg_color(s_clock_line, lv_color_hex(CLR_ACCENT), 0);
    lv_obj_set_style_bg_opa(s_clock_line, LV_OPA_70, 0);
    lv_obj_set_style_border_width(s_clock_line, 0, 0);
    lv_obj_set_style_radius(s_clock_line, 1, 0);
    lv_obj_remove_flag(s_clock_line, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(s_clock_line, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(s_clock_line, LV_ALIGN_TOP_MID, 0, 92);

    create_battery_indicator(s_clock_group);
    clock_update_time();
    power_status_update();

    /* Posição inicial: centrada. */
    lv_obj_update_layout(s_clock_group);
    int32_t scr_w = lv_obj_get_width(s_main_screen);
    int32_t scr_h = lv_obj_get_height(s_main_screen);
    int32_t x = (scr_w - lv_obj_get_width(s_clock_group)) / 2;
    int32_t y = (scr_h - lv_obj_get_height(s_clock_group)) / 2;
    if (x < SAFE_MARGIN_X) x = SAFE_MARGIN_X;
    if (y < SAFE_MARGIN_X) y = SAFE_MARGIN_X;
    if (x + lv_obj_get_width(s_clock_group) > scr_w - SAFE_MARGIN_X) {
        x = scr_w - SAFE_MARGIN_X - lv_obj_get_width(s_clock_group);
    }
    if (y + lv_obj_get_height(s_clock_group) > scr_h - SAFE_MARGIN_BOTTOM) {
        y = scr_h - SAFE_MARGIN_BOTTOM - lv_obj_get_height(s_clock_group);
    }
    lv_obj_set_pos(s_clock_group, x, y);

    s_clock_tick_timer = lv_timer_create(clock_tick_cb, 1000, NULL);
    s_clock_move_timer = lv_timer_create(clock_move_cb, CLOCK_MOVE_PERIOD_MS, NULL);
    s_power_status_timer = lv_timer_create(power_status_timer_cb, CLOCK_MOVE_PERIOD_MS, NULL);
}

/* ============================================================================
 * Espera inicial por ligação remota
 * ==========================================================================*/
static void wait_timer_cb(lv_timer_t *timer)
{
    /* Entra em relógio assim que um cliente BLE liga, ou após o timeout. */
    if (ble_service_is_connected() || lv_tick_elaps(s_wait_start_ms) >= REMOTE_WAIT_MS) {
        lv_timer_delete(timer);
        s_wait_timer = NULL;
        enter_clock_mode();
    }
}

/* ============================================================================
 * Notificações recebidas por BLE (ícone + título + corpo)
 * ==========================================================================*/

/* Volta ao ecrã do relógio. Assume o lock LVGL já obtido. */
static void hide_notification_locked(void)
{
    ensure_clock_mode();                 /* cria/retoma o relógio no ecrã principal */
    lv_obj_add_flag(s_notif_screen, LV_OBJ_FLAG_HIDDEN);
}

static void hide_notification_ui(void)
{
    hide_notification_locked();
}

static void clear_notification_queue(void)
{
    for (uint8_t i = 0; i < NOTIF_QUEUE_LEN; i++) {
        free(s_notif_queue[i].icon_buf);
        memset(&s_notif_queue[i], 0, sizeof(s_notif_queue[i]));
    }
    s_notif_q_head = 0;
    s_notif_q_count = 0;
    update_notification_dots();
}

static bool notification_is_visible(void)
{
    return s_notif_screen && !lv_obj_has_flag(s_notif_screen, LV_OBJ_FLAG_HIDDEN);
}

static void update_notification_dots(void)
{
    if (!s_msg_dots) {
        return;
    }

    uint8_t total = s_notif_q_count + (notification_is_visible() ? 1 : 0);
    if (total <= 1) {
        lv_obj_add_flag(s_msg_dots, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    if (total > 5) {
        total = 5;
    }

    lv_obj_remove_flag(s_msg_dots, LV_OBJ_FLAG_HIDDEN);
    for (uint8_t i = 0; i < 5; i++) {
        if (i < total) {
            lv_obj_remove_flag(s_msg_dot[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_bg_color(s_msg_dot[i],
                                      i == 0 ? lv_color_hex(CLR_ACCENT) : lv_color_hex(0x3A3F42),
                                      0);
        } else {
            lv_obj_add_flag(s_msg_dot[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static notif_item_t *queue_tail_slot(void)
{
    if (s_notif_q_count >= NOTIF_QUEUE_LEN) {
        notif_item_t *drop = &s_notif_queue[s_notif_q_head];
        free(drop->icon_buf);
        memset(drop, 0, sizeof(*drop));
        s_notif_q_head = (s_notif_q_head + 1) % NOTIF_QUEUE_LEN;
        s_notif_q_count--;
    }

    uint8_t idx = (s_notif_q_head + s_notif_q_count) % NOTIF_QUEUE_LEN;
    s_notif_q_count++;
    return &s_notif_queue[idx];
}

static void queue_current_notification(bool has_icon)
{
    notif_item_t *item = queue_tail_slot();
    strlcpy(item->title, s_n_title, sizeof(item->title));
    strlcpy(item->body, s_n_body, sizeof(item->body));
    item->has_icon = has_icon;
    item->icon_w = s_icon_w;
    item->icon_h = s_icon_h;
    item->icon_len = has_icon ? s_icon_len : 0;

    if (has_icon && s_icon_buf && s_icon_len) {
        item->icon_buf = heap_caps_malloc(s_icon_len, MALLOC_CAP_SPIRAM);
        if (!item->icon_buf) {
            item->icon_buf = malloc(s_icon_len);
        }
        if (item->icon_buf) {
            memcpy(item->icon_buf, s_icon_buf, s_icon_len);
        } else {
            item->has_icon = false;
            item->icon_len = 0;
        }
    }
    update_notification_dots();
}

static bool pop_queued_notification(void)
{
    if (s_notif_q_count == 0) {
        return false;
    }

    notif_item_t *item = &s_notif_queue[s_notif_q_head];
    strlcpy(s_n_title, item->title, sizeof(s_n_title));
    strlcpy(s_n_body, item->body, sizeof(s_n_body));
    s_icon_w = item->icon_w;
    s_icon_h = item->icon_h;
    s_icon_len = item->icon_len;

    if (item->has_icon && item->icon_buf && item->icon_len) {
        if (s_icon_cap < item->icon_len) {
            free(s_icon_buf);
            s_icon_buf = heap_caps_malloc(item->icon_len, MALLOC_CAP_SPIRAM);
            if (!s_icon_buf) {
                s_icon_buf = malloc(item->icon_len);
            }
            s_icon_cap = s_icon_buf ? item->icon_len : 0;
        }
        if (s_icon_buf) {
            memcpy(s_icon_buf, item->icon_buf, item->icon_len);
        }
    }

    bool has_icon = item->has_icon && s_icon_buf;
    free(item->icon_buf);
    memset(item, 0, sizeof(*item));
    s_notif_q_head = (s_notif_q_head + 1) % NOTIF_QUEUE_LEN;
    s_notif_q_count--;
    return has_icon;
}

static void show_next_queued_notification(void)
{
    bool has_icon = pop_queued_notification();
    show_notification_ui(has_icon);
}

/* Toque no ecrã enquanto a notificação está visível -> limpa e volta ao relógio. */
static void msg_clicked_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    if (s_notif_q_count > 0) {
        show_next_queued_notification();
    } else {
        hide_notification_locked();
    }
}

/* Monta e mostra a notificação acumulada (chamado da task BLE -> usa o lock). */
static void show_notification_ui(bool has_icon)
{
    if (!s_notif_screen) {
        return;
    }
    bsp_display_brightness_set(ACTIVE_BRIGHTNESS);

    if (has_icon) {
        if (s_display_icon_cap < s_icon_len) {
            free(s_display_icon_buf);
            s_display_icon_buf = heap_caps_malloc(s_icon_len, MALLOC_CAP_SPIRAM);
            if (!s_display_icon_buf) {
                s_display_icon_buf = malloc(s_icon_len);
            }
            s_display_icon_cap = s_display_icon_buf ? s_icon_len : 0;
        }
        if (!s_display_icon_buf) {
            has_icon = false;
        }
    }

    if (has_icon) {
        memcpy(s_display_icon_buf, s_icon_buf, s_icon_len);
        s_icon_dsc.header.magic  = LV_IMAGE_HEADER_MAGIC;
        s_icon_dsc.header.cf     = LV_COLOR_FORMAT_RGB565;
        s_icon_dsc.header.flags  = 0;
        s_icon_dsc.header.w      = s_icon_w;
        s_icon_dsc.header.h      = s_icon_h;
        s_icon_dsc.header.stride = (uint32_t)s_icon_w * 2;
        s_icon_dsc.data          = s_display_icon_buf;
        s_icon_dsc.data_size     = s_icon_len;
        /* O ponteiro do dsc é reutilizado: limpar a cache para não mostrar a
         * imagem anterior. */
        lv_image_cache_drop(&s_icon_dsc);
        lv_image_set_src(s_msg_icon, &s_icon_dsc);
        lv_obj_remove_flag(s_msg_icon_box, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_image_set_src(s_msg_icon, NULL);
        lv_obj_add_flag(s_msg_icon_box, LV_OBJ_FLAG_HIDDEN);
    }

    bool has_title = (s_n_title[0] != '\0');
    lv_label_set_text(s_notif_header, has_title ? "NOTIFICAÇÃO" : "MENSAGEM");
    lv_label_set_text(s_msg_title, s_n_title);
    if (has_title) {
        /* Com ícone: título à esquerda, ao lado do ícone. Sem ícone: centrado. */
        lv_obj_set_style_text_align(s_msg_title,
                                    has_icon ? LV_TEXT_ALIGN_LEFT : LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_remove_flag(s_msg_title, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_msg_divider, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_msg_title, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_msg_divider, LV_OBJ_FLAG_HIDDEN);
    }

    /* A linha (ícone+título) só faz sentido se houver pelo menos um dos dois. */
    if (has_icon || has_title) {
        lv_obj_remove_flag(s_msg_row, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_msg_row, LV_OBJ_FLAG_HIDDEN);
    }

    lv_label_set_text(s_msg_body, s_n_body);

    /* Pausa o relógio enquanto a notificação está visível (poupa CPU; a animação
     * de movimento não corre por baixo do painel opaco). */
    if (s_clock_group) {
        lv_anim_delete(s_clock_group, NULL);
    }
    if (s_clock_tick_timer) {
        lv_timer_pause(s_clock_tick_timer);
    }
    if (s_clock_move_timer) {
        lv_timer_pause(s_clock_move_timer);
    }
    if (s_wait_timer) {
        lv_timer_pause(s_wait_timer);
    }
    if (s_power_status_timer) {
        lv_timer_pause(s_power_status_timer);
    }

    lv_obj_scroll_to_y(s_notif_screen, 0, LV_ANIM_OFF);
    lv_obj_move_foreground(s_notif_screen);
    lv_obj_remove_flag(s_notif_screen, LV_OBJ_FLAG_HIDDEN);
    /* Entrada suave do cartão (fade-in). */
    lv_obj_fade_in(s_notif_card, 220, 0);
    update_notification_dots();
}

static void show_notification_async_cb(void *user_data)
{
    bool has_icon = (bool)(uintptr_t)user_data;
    if (notification_is_visible()) {
        queue_current_notification(has_icon);
    } else {
        show_notification_ui(has_icon);
    }
}

static void hide_notification_async_cb(void *user_data)
{
    LV_UNUSED(user_data);
    clear_notification_queue();
    hide_notification_ui();
}

/* Callbacks do serviço BLE (correm na task do host NimBLE). */
static void on_ble_time(uint32_t epoch)
{
    struct timeval tv = { .tv_sec = (time_t)epoch, .tv_usec = 0 };
    settimeofday(&tv, NULL);
}

/* Mensagem de texto simples -> notificação só com corpo, sem ícone. */
static void on_ble_message(const char *text, uint16_t len)
{
    s_n_title[0] = '\0';
    uint16_t copy_len = len;
    if (copy_len >= sizeof(s_n_body)) {
        copy_len = sizeof(s_n_body) - 1;
    }
    memcpy(s_n_body, text, copy_len);
    s_n_body[copy_len] = '\0';
    sanitize_text(s_n_body);
    s_icon_w = 0;
    schedule_lvgl(show_notification_async_cb, (void *)(uintptr_t)false);
}

/* Frame da característica de notificação (framing OP_*). */
static void on_ble_notify(const uint8_t *data, uint16_t len)
{
    uint8_t op = data[0];
    const uint8_t *p = data + 1;
    uint16_t n = len - 1;

    switch (op) {
    case OP_TEXT: {
        /* p = titulo \0 corpo */
        uint16_t i = 0;
        while (i < n && p[i] != 0) {
            i++;
        }
        uint16_t tlen = i;
        if (tlen >= sizeof(s_n_title)) {
            tlen = sizeof(s_n_title) - 1;
        }
        memcpy(s_n_title, p, tlen);
        s_n_title[tlen] = '\0';
        sanitize_text(s_n_title);

        uint16_t bstart = (i < n) ? (i + 1) : n;
        uint16_t blen = n - bstart;
        if (blen >= sizeof(s_n_body)) {
            blen = sizeof(s_n_body) - 1;
        }
        memcpy(s_n_body, p + bstart, blen);
        s_n_body[blen] = '\0';
        sanitize_text(s_n_body);
        break;
    }

    case OP_ICON_BEGIN: {
        s_icon_w = 0;
        s_icon_len = 0;
        if (n >= 4) {
            uint16_t w = (uint16_t)p[0] | ((uint16_t)p[1] << 8);
            uint16_t h = (uint16_t)p[2] | ((uint16_t)p[3] << 8);
            if (w > 0 && h > 0 && w <= NOTIF_ICON_MAX_W && h <= NOTIF_ICON_MAX_H) {
                uint32_t bytes = (uint32_t)w * h * 2;
                if (s_icon_cap < bytes) {
                    free(s_icon_buf);
                    s_icon_buf = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
                    if (!s_icon_buf) {
                        s_icon_buf = malloc(bytes);
                    }
                    s_icon_cap = s_icon_buf ? bytes : 0;
                }
                if (s_icon_buf) {
                    s_icon_w = w;
                    s_icon_h = h;
                }
            }
        }
        break;
    }

    case OP_ICON_DATA:
        if (s_icon_buf && s_icon_w > 0 && (s_icon_len + n) <= s_icon_cap) {
            memcpy(s_icon_buf + s_icon_len, p, n);
            s_icon_len += n;
        }
        break;

    case OP_COMMIT: {
        bool has_icon = (s_icon_w > 0 && s_icon_buf &&
                         s_icon_len == (uint32_t)s_icon_w * s_icon_h * 2);
        schedule_lvgl(show_notification_async_cb, (void *)(uintptr_t)has_icon);
        break;
    }

    case OP_CLEAR:
        schedule_lvgl(hide_notification_async_cb, NULL);
        break;

    default:
        break;
    }
}

/* ============================================================================
 * Botão PWR — contador 3..0 sobre painel preto (overlay)
 * ==========================================================================*/
static void power_btn_task(void *arg)
{
    LV_UNUSED(arg);
    bool counting = false;
    int count = 0;
    TickType_t next_tick = 0;

    while (1) {
        power_btn_event_t ev = power_button_poll();

        if (ev == POWER_BTN_PRESSED && !counting) {
            counting = true;
            count = POWEROFF_COUNT_FROM;
            next_tick = xTaskGetTickCount() + pdMS_TO_TICKS(1000);

            bsp_display_lock(0);
            bsp_display_brightness_set(ACTIVE_BRIGHTNESS);
            lv_label_set_text_fmt(s_countdown_label, "%d", count);
            lv_obj_remove_flag(s_countdown_panel, LV_OBJ_FLAG_HIDDEN);  /* top layer: já fica por cima */
            bsp_display_unlock();
        } else if (ev == POWER_BTN_RELEASED && counting) {
            /* Largou antes de chegar a 0 -> cancela. */
            counting = false;
            bsp_display_lock(0);
            lv_obj_add_flag(s_countdown_panel, LV_OBJ_FLAG_HIDDEN);
            if (s_clock_label && lv_obj_has_flag(s_notif_screen, LV_OBJ_FLAG_HIDDEN)) {
                bsp_display_brightness_set(CLOCK_BRIGHTNESS);
            }
            bsp_display_unlock();
        }

        if (counting && (int)(xTaskGetTickCount() - next_tick) >= 0) {
            next_tick += pdMS_TO_TICKS(1000);
            if (count > 0) {
                count--;
            }
            bsp_display_lock(0);
            lv_label_set_text_fmt(s_countdown_label, "%d", count);
            bsp_display_unlock();
            /* Ao chegar a 0 o AXP2101 corta a energia (hardware). */
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

/* ============================================================================
 * UI
 * ==========================================================================*/
/* Aplica fundo preto opaco e sem padding a um ecrã. */
static void style_black_screen(lv_obj_t *screen)
{
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_set_style_radius(screen, 0, 0);
    lv_obj_set_style_outline_width(screen, 0, 0);
}

static void create_ui(void)
{
    /* --- Ecrã principal: boas-vindas + relógio. --- */
    s_main_screen = lv_screen_active();
    style_black_screen(s_main_screen);
    lv_obj_set_style_pad_all(s_main_screen, 0, 0);

    s_welcome_label = lv_label_create(s_main_screen);
    lv_label_set_text(s_welcome_label, "Welcome to Desk Companion");
    lv_obj_set_style_text_color(s_welcome_label, lv_color_hex(CLR_TEXT), 0);
    lv_obj_set_style_text_font(s_welcome_label, &font_chakra_title_30, 0);
    lv_obj_set_style_text_letter_space(s_welcome_label, 1, 0);
    lv_obj_set_style_text_align(s_welcome_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_welcome_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_welcome_label, LV_PCT(90));
    lv_obj_center(s_welcome_label);

    /* --- Painel de notificação: ícone + título + divisória + corpo. --- */
    s_notif_screen = lv_obj_create(s_main_screen);
    lv_obj_set_size(s_notif_screen, LV_PCT(100), LV_PCT(100));
    style_black_screen(s_notif_screen);
    lv_obj_set_style_pad_hor(s_notif_screen, 28, 0);
    lv_obj_set_style_pad_ver(s_notif_screen, 24, 0);
    lv_obj_set_style_pad_row(s_notif_screen, 16, 0);
    lv_obj_set_flex_flow(s_notif_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_notif_screen, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollbar_mode(s_notif_screen, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(s_notif_screen, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s_notif_screen, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(s_notif_screen, msg_clicked_cb, LV_EVENT_CLICKED, NULL);

    /* Cabeçalho discreto em maiúsculas espaçadas (toque "HUD"/tech). */
    s_notif_header = lv_label_create(s_notif_screen);
    lv_label_set_text(s_notif_header, "MENSAGEM");
    lv_obj_set_style_text_color(s_notif_header, lv_color_hex(CLR_ACCENT), 0);
    lv_obj_set_style_text_font(s_notif_header, &font_chakra_18, 0);
    lv_obj_set_style_text_letter_space(s_notif_header, 4, 0);
    lv_obj_set_style_text_align(s_notif_header, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_remove_flag(s_notif_header, LV_OBJ_FLAG_CLICKABLE);

    /* Cartão central: fundo preto + moldura fina arredondada. */
    s_notif_card = lv_obj_create(s_notif_screen);
    lv_obj_set_width(s_notif_card, LV_PCT(100));
    lv_obj_set_height(s_notif_card, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(s_notif_card, lv_color_hex(0x0B0D0E), 0);
    lv_obj_set_style_bg_opa(s_notif_card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_notif_card, lv_color_hex(CLR_ACCENT), 0);
    lv_obj_set_style_border_width(s_notif_card, 1, 0);
    lv_obj_set_style_border_opa(s_notif_card, LV_OPA_50, 0);
    lv_obj_set_style_radius(s_notif_card, 20, 0);
    lv_obj_set_style_pad_all(s_notif_card, 22, 0);
    lv_obj_set_style_pad_row(s_notif_card, 14, 0);
    lv_obj_set_flex_flow(s_notif_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_notif_card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollbar_mode(s_notif_card, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(s_notif_card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(s_notif_card, LV_OBJ_FLAG_CLICKABLE);

    /* Linha do topo: coluna do ícone (esquerda) + coluna do título (direita,
     * maior). O título alinha verticalmente ao centro do ícone. */
    s_msg_row = lv_obj_create(s_notif_card);
    lv_obj_set_width(s_msg_row, LV_PCT(100));
    lv_obj_set_height(s_msg_row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(s_msg_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_msg_row, 0, 0);
    lv_obj_set_style_pad_all(s_msg_row, 0, 0);
    lv_obj_set_style_pad_column(s_msg_row, 14, 0);
    lv_obj_set_flex_flow(s_msg_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_msg_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollbar_mode(s_msg_row, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(s_msg_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(s_msg_row, LV_OBJ_FLAG_CLICKABLE);

    /* Coluna 1 — ícone: sem borda, com um glow subtil à volta (toque tech). */
    s_msg_icon_box = lv_obj_create(s_msg_row);
    lv_obj_set_size(s_msg_icon_box, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(s_msg_icon_box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(s_msg_icon_box, 3, 0);
    lv_obj_set_style_radius(s_msg_icon_box, 16, 0);
    lv_obj_set_style_border_width(s_msg_icon_box, 0, 0);
    lv_obj_set_style_shadow_color(s_msg_icon_box, lv_color_hex(CLR_ACCENT), 0);
    lv_obj_set_style_shadow_width(s_msg_icon_box, 16, 0);
    lv_obj_set_style_shadow_spread(s_msg_icon_box, 0, 0);
    lv_obj_set_style_shadow_opa(s_msg_icon_box, LV_OPA_40, 0);
    lv_obj_add_flag(s_msg_icon_box, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_msg_icon_box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(s_msg_icon_box, LV_OBJ_FLAG_CLICKABLE);

    s_msg_icon = lv_image_create(s_msg_icon_box);
    lv_obj_set_style_radius(s_msg_icon, 14, 0);
    lv_obj_set_style_clip_corner(s_msg_icon, true, 0);
    lv_obj_set_style_border_width(s_msg_icon, 0, 0);
    lv_obj_remove_flag(s_msg_icon, LV_OBJ_FLAG_CLICKABLE);

    /* Coluna 2 — título: ocupa o espaço restante (coluna maior). */
    s_msg_title = lv_label_create(s_msg_row);
    lv_label_set_text(s_msg_title, "");
    lv_obj_set_flex_grow(s_msg_title, 1);
    lv_obj_set_style_text_color(s_msg_title, lv_color_hex(CLR_TEXT), 0);
    lv_obj_set_style_text_font(s_msg_title, &font_chakra_title_30, 0);
    lv_obj_set_style_text_align(s_msg_title, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_long_mode(s_msg_title, LV_LABEL_LONG_WRAP);
    lv_obj_remove_flag(s_msg_title, LV_OBJ_FLAG_CLICKABLE);

    s_msg_divider = lv_obj_create(s_notif_card);
    lv_obj_set_size(s_msg_divider, 48, 2);
    lv_obj_set_style_bg_color(s_msg_divider, lv_color_hex(CLR_ACCENT), 0);
    lv_obj_set_style_bg_opa(s_msg_divider, LV_OPA_70, 0);
    lv_obj_set_style_border_width(s_msg_divider, 0, 0);
    lv_obj_set_style_radius(s_msg_divider, 1, 0);
    lv_obj_remove_flag(s_msg_divider, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(s_msg_divider, LV_OBJ_FLAG_SCROLLABLE);

    s_msg_body = lv_label_create(s_notif_card);
    lv_label_set_text(s_msg_body, "");
    lv_obj_set_style_text_color(s_msg_body, lv_color_hex(CLR_TEXT_DIM), 0);
    lv_obj_set_style_text_font(s_msg_body, &font_chakra_body_24, 0);
    lv_obj_set_style_text_align(s_msg_body, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_msg_body, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_msg_body, LV_PCT(100));
    lv_obj_remove_flag(s_msg_body, LV_OBJ_FLAG_CLICKABLE);

    s_msg_dots = lv_obj_create(s_notif_screen);
    lv_obj_set_size(s_msg_dots, 72, 12);
    lv_obj_align(s_msg_dots, LV_ALIGN_BOTTOM_MID, 0, -14);
    lv_obj_add_flag(s_msg_dots, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_add_flag(s_msg_dots, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_opa(s_msg_dots, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_msg_dots, 0, 0);
    lv_obj_set_style_pad_all(s_msg_dots, 0, 0);
    lv_obj_set_style_pad_column(s_msg_dots, 6, 0);
    lv_obj_set_flex_flow(s_msg_dots, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_msg_dots, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(s_msg_dots, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(s_msg_dots, LV_OBJ_FLAG_CLICKABLE);

    for (uint8_t i = 0; i < 5; i++) {
        s_msg_dot[i] = lv_obj_create(s_msg_dots);
        lv_obj_set_size(s_msg_dot[i], 8, 8);
        lv_obj_set_style_bg_color(s_msg_dot[i], i == 0 ? lv_color_hex(CLR_ACCENT) : lv_color_hex(0x3A3F42), 0);
        lv_obj_set_style_bg_opa(s_msg_dot[i], LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(s_msg_dot[i], 0, 0);
        lv_obj_set_style_radius(s_msg_dot[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_remove_flag(s_msg_dot[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(s_msg_dot[i], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(s_msg_dot[i], LV_OBJ_FLAG_HIDDEN);
    }

    /* --- Contador do PWR na TOP LAYER (fica sempre por cima de qualquer ecrã). --- */
    lv_obj_t *top = lv_layer_top();
    s_countdown_panel = lv_obj_create(top);
    lv_obj_set_size(s_countdown_panel, LV_PCT(100), LV_PCT(100));
    style_black_screen(s_countdown_panel);
    lv_obj_set_style_pad_all(s_countdown_panel, 0, 0);
    lv_obj_remove_flag(s_countdown_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_countdown_panel, LV_OBJ_FLAG_HIDDEN);

    s_countdown_label = lv_label_create(s_countdown_panel);
    lv_label_set_text(s_countdown_label, "3");
    lv_obj_set_style_text_color(s_countdown_label, lv_color_hex(CLR_TEXT), 0);
    lv_obj_set_style_text_font(s_countdown_label, &font_chakra_64, 0);
    lv_obj_center(s_countdown_label);

    /* Anel de acento fino à volta do contador (toque "tech"). */
    lv_obj_t *cd_ring = lv_obj_create(s_countdown_panel);
    lv_obj_set_size(cd_ring, 140, 140);
    lv_obj_center(cd_ring);
    lv_obj_move_background(cd_ring);
    lv_obj_set_style_bg_opa(cd_ring, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(cd_ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_color(cd_ring, lv_color_hex(CLR_ACCENT), 0);
    lv_obj_set_style_border_width(cd_ring, 2, 0);
    lv_obj_set_style_border_opa(cd_ring, LV_OPA_60, 0);
    lv_obj_remove_flag(cd_ring, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(cd_ring, LV_OBJ_FLAG_CLICKABLE);
}

void app_main(void)
{
    /* NVS é necessário para o stack BLE (calibração PHY, etc.). */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    bsp_display_start();

    ESP_ERROR_CHECK(power_button_init());

    bsp_display_lock(0);

    create_ui();

    /* Inicia a espera (~30 s) por uma ligação remota; ao expirar -> relógio. */
    s_wait_start_ms = lv_tick_get();
    s_wait_timer = lv_timer_create(wait_timer_cb, 500, NULL);

    bsp_display_unlock();

    /* Arranca o serviço BLE (hora + mensagens + notificações). */
    ESP_ERROR_CHECK(ble_service_start(BLE_DEVICE_NAME, on_ble_time, on_ble_message, on_ble_notify));

    xTaskCreate(power_btn_task, "power_btn", 4096, NULL, 5, NULL);
}
