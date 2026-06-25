idf_component_register(
    SRCS "espir_oled.c"
    INCLUDE_DIRS "include"
    REQUIRES driver espir_device espir_zcl
)
