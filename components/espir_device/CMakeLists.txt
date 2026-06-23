idf_component_register(
    SRCS "espir_device.c"
    INCLUDE_DIRS "include"
    REQUIRES espir_zcl espir_ir espir_store espir_codec
             espressif__esp-zigbee-lib espressif__esp-zboss-lib
)
