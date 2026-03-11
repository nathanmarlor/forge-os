idf_component_register(
SRCS
    "utils.c"
    "mining.c"
    "stratum_api.c"
    "base58.c"
    "segwit_addr.c"
    "coinbase_decoder.c"

INCLUDE_DIRS
    "include"

REQUIRES
    "cjson"
    "mbedtls"
    "app_update"
)

# mbedtls v4: private SHA headers live under tf-psa-crypto/drivers/builtin/include
idf_build_get_property(idf_path IDF_PATH)
target_include_directories(${COMPONENT_LIB} PRIVATE
    "${idf_path}/components/mbedtls/mbedtls/tf-psa-crypto/drivers/builtin/include"
)