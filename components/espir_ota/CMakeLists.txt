idf_component_register(
    SRCS "espir_ota.c"
    INCLUDE_DIRS "include"
    REQUIRES espir_zcl app_update
             espressif__esp-zigbee-lib espressif__esp-zboss-lib
)
