#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Comprimento máximo (bytes) de uma mensagem de texto recebida por BLE. */
#define DC_MSG_MAX_LEN  240

/* Callback chamado quando o host envia uma nova hora.
 * @param epoch  segundos Unix que representam a hora LOCAL a mostrar (uint32 LE). */
typedef void (*ble_time_cb_t)(uint32_t epoch);

/* Callback chamado quando o host envia uma mensagem de texto simples.
 * @param text  string UTF-8 terminada em '\0' (válida só durante o callback).
 * @param len   número de bytes úteis (sem o terminador). */
typedef void (*ble_msg_cb_t)(const char *text, uint16_t len);

/* Callback chamado a cada frame escrito na característica de notificação.
 * O protocolo de framing (opcodes) é interpretado pela camada da aplicação.
 * @param data  bytes do frame (válidos só durante o callback).
 * @param len   número de bytes do frame. */
typedef void (*ble_notify_cb_t)(const uint8_t *data, uint16_t len);

/**
 * @brief Arranca o serviço BLE (NimBLE) e começa a anunciar.
 *
 * @param device_name  nome anunciado (ex.: "Desk Companion").
 * @param on_time      callback de atualização de hora (pode ser NULL).
 * @param on_msg       callback de mensagem de texto simples (pode ser NULL).
 * @param on_notify    callback de frame de notificação rica (pode ser NULL).
 *
 * @note Os callbacks correm na task do host NimBLE — não chamar APIs LVGL
 *       sem antes obter `bsp_display_lock()`.
 */
esp_err_t ble_service_start(const char *device_name,
                            ble_time_cb_t on_time,
                            ble_msg_cb_t on_msg,
                            ble_notify_cb_t on_notify);

/** @brief true se houver um cliente BLE ligado. */
bool ble_service_is_connected(void);

#ifdef __cplusplus
}
#endif
