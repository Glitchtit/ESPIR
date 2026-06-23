idf_component_register(
    SRCS "espir_ir.c"
    INCLUDE_DIRS "include"
    REQUIRES espir_codec espir_zcl driver esp_timer
)
