idf_component_register(SRCS "main.cpp"
                            "sensor_task.cpp"
                            "telemetry_task.cpp"
                            "sd_task.cpp"
                            "network_task.cpp"
                    INCLUDE_DIRS "."
                    REQUIRES esp_dsp mqtt nvs_flash fatfs sdmmc vfs esp_adc)
