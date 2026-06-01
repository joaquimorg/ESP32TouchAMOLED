idf_component_register(
    SRCS main.c power_button.c ble_service.c
         fonts/font_chakra_64.c
         fonts/font_chakra_sec_30.c
         fonts/font_chakra_title_30.c
         fonts/font_chakra_body_24.c
         fonts/font_chakra_18.c
    INCLUDE_DIRS . fonts
    REQUIRES esp_driver_i2c bt nvs_flash)

# As fontes geradas (fonts/*.c) incluem o LVGL via "lvgl.h" quando este macro
# está definido; sem ele usam "lvgl/lvgl.h", que não resolve no componente main.
target_compile_definitions(${COMPONENT_LIB} PRIVATE LV_LVGL_H_INCLUDE_SIMPLE)

idf_component_get_property(LVGL_LIB lvgl__lvgl COMPONENT_LIB)
target_compile_options(
    ${LVGL_LIB}
    PRIVATE
        -DLV_LVGL_H_INCLUDE_SIMPLE
)
