idf_component_register(SRCS "main_dep.cpp"
                            "sensor_task.cpp"
                            "telemetry_task.cpp"
                            "sd_task.cpp"
                            "network_task.cpp"
                    INCLUDE_DIRS "."
                    REQUIRES esp-dsp mqtt nvs_flash fatfs sdmmc vfs esp_adc esp_driver_uart esp_wifi driver esp_driver_i2c cjson esp_driver_gptimer)
