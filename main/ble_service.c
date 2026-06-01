#include "ble_service.h"

#include <string.h>
#include <inttypes.h>
#include "esp_log.h"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_gatt.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

static const char *TAG = "ble_service";

/* ----------------------------------------------------------------------------
 * UUIDs do serviço Desk Companion (128-bit)
 *
 *   Serviço : e3f10000-8a1b-4c2d-9e5f-1a2b3c4d5e6f
 *   Hora    : e3f10001-8a1b-4c2d-9e5f-1a2b3c4d5e6f  (Write)
 *   Mensagem: e3f10002-8a1b-4c2d-9e5f-1a2b3c4d5e6f  (Write)
 *
 * BLE_UUID128_INIT recebe os bytes em little-endian (ordem inversa da forma
 * humana acima); só o byte índice 12 muda entre serviço/hora/mensagem.
 * ------------------------------------------------------------------------- */
static const ble_uuid128_t s_svc_uuid = BLE_UUID128_INIT(
    0x6f, 0x5e, 0x4d, 0x3c, 0x2b, 0x1a, 0x5f, 0x9e,
    0x2d, 0x4c, 0x1b, 0x8a, 0x00, 0x00, 0xf1, 0xe3);
static const ble_uuid128_t s_time_uuid = BLE_UUID128_INIT(
    0x6f, 0x5e, 0x4d, 0x3c, 0x2b, 0x1a, 0x5f, 0x9e,
    0x2d, 0x4c, 0x1b, 0x8a, 0x01, 0x00, 0xf1, 0xe3);
static const ble_uuid128_t s_msg_uuid = BLE_UUID128_INIT(
    0x6f, 0x5e, 0x4d, 0x3c, 0x2b, 0x1a, 0x5f, 0x9e,
    0x2d, 0x4c, 0x1b, 0x8a, 0x02, 0x00, 0xf1, 0xe3);
static const ble_uuid128_t s_notify_uuid = BLE_UUID128_INIT(
    0x6f, 0x5e, 0x4d, 0x3c, 0x2b, 0x1a, 0x5f, 0x9e,
    0x2d, 0x4c, 0x1b, 0x8a, 0x03, 0x00, 0xf1, 0xe3);

static ble_time_cb_t   s_on_time   = NULL;
static ble_msg_cb_t    s_on_msg    = NULL;
static ble_notify_cb_t s_on_notify = NULL;
static char            s_device_name[32] = "Desk Companion";

static uint8_t  s_own_addr_type;
static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;

static void ble_advertise(void);

/* ----------------------------------------------------------------------------
 * Acesso às características (write)
 * ------------------------------------------------------------------------- */
static int chr_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                         struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
        return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
    }

    /* Espaço para o maior write possível (ATT_MTU 247 -> 244 bytes úteis). */
    uint8_t  buf[256];
    uint16_t len = 0;
    int rc = ble_hs_mbuf_to_flat(ctxt->om, buf, sizeof(buf) - 1, &len);
    if (rc != 0) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    if (ble_uuid_cmp(ctxt->chr->uuid, &s_time_uuid.u) == 0) {
        if (len >= 4 && s_on_time) {
            uint32_t epoch = (uint32_t)buf[0]
                           | ((uint32_t)buf[1] << 8)
                           | ((uint32_t)buf[2] << 16)
                           | ((uint32_t)buf[3] << 24);
            ESP_LOGI(TAG, "Hora recebida: epoch=%" PRIu32, epoch);
            s_on_time(epoch);
        }
        return 0;
    }

    if (ble_uuid_cmp(ctxt->chr->uuid, &s_msg_uuid.u) == 0) {
        buf[len] = '\0';
        ESP_LOGI(TAG, "Mensagem recebida (%u bytes)", len);
        if (s_on_msg) {
            s_on_msg((const char *)buf, len);
        }
        return 0;
    }

    if (ble_uuid_cmp(ctxt->chr->uuid, &s_notify_uuid.u) == 0) {
        if (s_on_notify && len > 0) {
            s_on_notify(buf, len);
        }
        return 0;
    }

    return BLE_ATT_ERR_UNLIKELY;
}

static const struct ble_gatt_svc_def s_gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]){
            {
                .uuid      = &s_time_uuid.u,
                .access_cb = chr_access_cb,
                .flags     = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
            },
            {
                .uuid      = &s_msg_uuid.u,
                .access_cb = chr_access_cb,
                .flags     = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
            },
            {
                .uuid      = &s_notify_uuid.u,
                .access_cb = chr_access_cb,
                .flags     = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
            },
            { 0 } /* fim das características */
        },
    },
    { 0 } /* fim dos serviços */
};

/* ----------------------------------------------------------------------------
 * GAP / advertising
 * ------------------------------------------------------------------------- */
static int ble_gap_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            s_conn_handle = event->connect.conn_handle;
            ESP_LOGI(TAG, "Cliente ligado (handle=%d)", s_conn_handle);
        } else {
            ESP_LOGW(TAG, "Falha na ligacao; volta a anunciar");
            ble_advertise();
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "Cliente desligado (reason=%d)", event->disconnect.reason);
        s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        ble_advertise();
        return 0;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        ble_advertise();
        return 0;

    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "MTU negociado: %d", event->mtu.value);
        return 0;

    default:
        return 0;
    }
}

static void ble_advertise(void)
{
    struct ble_hs_adv_fields fields = {0};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name = (uint8_t *)s_device_name;
    fields.name_len = strlen(s_device_name);
    fields.name_is_complete = 1;

    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_adv_set_fields rc=%d", rc);
        return;
    }

    struct ble_gap_adv_params adv_params = {0};
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    rc = ble_gap_adv_start(s_own_addr_type, NULL, BLE_HS_FOREVER,
                           &adv_params, ble_gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_adv_start rc=%d", rc);
    } else {
        ESP_LOGI(TAG, "A anunciar como \"%s\"", s_device_name);
    }
}

static void ble_on_sync(void)
{
    int rc = ble_hs_id_infer_auto(0, &s_own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_hs_id_infer_auto rc=%d", rc);
        return;
    }
    ble_advertise();
}

static void ble_on_reset(int reason)
{
    ESP_LOGW(TAG, "BLE reset; reason=%d", reason);
}

static void ble_host_task(void *param)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

/* ----------------------------------------------------------------------------
 * API pública
 * ------------------------------------------------------------------------- */
bool ble_service_is_connected(void)
{
    return s_conn_handle != BLE_HS_CONN_HANDLE_NONE;
}

esp_err_t ble_service_start(const char *device_name,
                            ble_time_cb_t on_time,
                            ble_msg_cb_t on_msg,
                            ble_notify_cb_t on_notify)
{
    s_on_time   = on_time;
    s_on_msg    = on_msg;
    s_on_notify = on_notify;
    if (device_name) {
        strncpy(s_device_name, device_name, sizeof(s_device_name) - 1);
        s_device_name[sizeof(s_device_name) - 1] = '\0';
    }

    esp_err_t ret = nimble_port_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nimble_port_init falhou: %d", ret);
        return ret;
    }

    ble_svc_gap_init();
    ble_svc_gatt_init();

    int rc = ble_gatts_count_cfg(s_gatt_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gatts_count_cfg rc=%d", rc);
        return ESP_FAIL;
    }
    rc = ble_gatts_add_svcs(s_gatt_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gatts_add_svcs rc=%d", rc);
        return ESP_FAIL;
    }

    ble_svc_gap_device_name_set(s_device_name);

    ble_hs_cfg.sync_cb  = ble_on_sync;
    ble_hs_cfg.reset_cb = ble_on_reset;

    nimble_port_freertos_init(ble_host_task);

    ESP_LOGI(TAG, "Servico BLE iniciado");
    return ESP_OK;
}
