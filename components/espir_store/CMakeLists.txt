idf_component_register(
    SRCS "espir_store.c"
    INCLUDE_DIRS "include"
    REQUIRES espir_codec espir_zcl nvs_flash
)
