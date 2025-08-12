idf_component_register(SRCS "esp32_s3_szp.c" "main.c"
                    INCLUDE_DIRS ".")
                    
spiffs_create_partition_image(storage ${CMAKE_CURRENT_SOURCE_DIR}/spiffs FLASH_IN_PROJECT)