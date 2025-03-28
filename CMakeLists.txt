file(GLOB SOURCES *.c)
SET(INCS "include")
SET(PRIVINCS ".")

idf_component_register(
    SRCS ${SOURCES}
    INCLUDE_DIRS ${INCS}
    PRIV_INCLUDE_DIRS ${PRIVINCS}
    REQUIRES esp_event logger_common
    PRIV_REQUIRES strutil
)
if(CONFIG_LOGGER_VFS_ENABLED)
    target_link_libraries(${COMPONENT_LIB} PRIVATE idf::esp_partition)
    if(CONFIG_USE_SD_CARD)
        target_link_libraries(${COMPONENT_LIB} PRIVATE idf::spi_flash)
        if(CONFIG_DEBUG_PIN_CONNECTIONS)
            target_link_libraries(${COMPONENT_LIB} PRIVATE idf::test_io)
        endif()
    endif()
    if(CONFIG_USE_FATFS OR CONFIG_USE_SD_CARD)
        target_link_libraries(${COMPONENT_LIB} PRIVATE idf::fatfs)
    endif()
    if(CONFIG_USE_SPIFFS)
        target_link_libraries(${COMPONENT_LIB} PRIVATE idf::spiffs)
    endif()
    if(CONFIG_USE_LITTLEFS)
        target_link_libraries(${COMPONENT_LIB} PRIVATE idf::littlefs)
    endif()
    if(NOT ${IDF_TARGET} STREQUAL "linux")
        target_link_libraries(${COMPONENT_LIB} PUBLIC idf::vfs)
        target_link_libraries(${COMPONENT_LIB} PRIVATE idf::driver)
    endif()
endif()