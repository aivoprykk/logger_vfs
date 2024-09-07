
set(REQ esp_event)
set(PRIVREQ esp_partition spi_flash fatfs spiffs logger_common driver)
file(GLOB SOURCES *.c)
SET(INCS "include")
SET(PRIVINCS ".")

idf_component_register(
    SRCS ${SOURCES}
    INCLUDE_DIRS ${INCS}
    PRIV_INCLUDE_DIRS ${PRIVINCS}
    REQUIRES ${REQ}
    PRIV_REQUIRES ${PRIVREQ}
)
