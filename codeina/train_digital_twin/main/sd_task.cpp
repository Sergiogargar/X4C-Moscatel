#include "sd_task.h"
#include "data_types.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/spi_master.h"
#include <string.h>
#include <unistd.h>

static const char *TAG = "SD_TASK";

#define PIN_NUM_MISO 13
#define PIN_NUM_MOSI 11
#define PIN_NUM_CLK  12
#define PIN_NUM_CS   10

void vSdTask(void *pvParameters) {
    ESP_LOGI(TAG, "Inicializando SD Task en Core %d", xPortGetCoreID());

    esp_vfs_fat_sdmmc_mount_config_t mount_config;
    memset(&mount_config, 0, sizeof(mount_config));
    mount_config.format_if_mount_failed = true;
    mount_config.max_files = 5;
    mount_config.allocation_unit_size = 16 * 1024;

    sdmmc_card_t *card;
    const char mount_point[] = "/sdcard";

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    spi_bus_config_t bus_cfg;
    memset(&bus_cfg, 0, sizeof(bus_cfg));
    bus_cfg.mosi_io_num = PIN_NUM_MOSI;
    bus_cfg.miso_io_num = PIN_NUM_MISO;
    bus_cfg.sclk_io_num = PIN_NUM_CLK;
    bus_cfg.quadwp_io_num = -1;
    bus_cfg.quadhd_io_num = -1;
    bus_cfg.max_transfer_sz = 4000;
    
    // Inicializar SPI bus
    esp_err_t ret = spi_bus_initialize((spi_host_device_t)host.slot, &bus_cfg, SDSPI_DEFAULT_DMA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Fallo al inicializar SPI bus.");
        vTaskDelete(NULL);
    }

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = (gpio_num_t)PIN_NUM_CS;
    slot_config.host_id = (spi_host_device_t)host.slot;

    ret = esp_vfs_fat_sdspi_mount(mount_point, &host, &slot_config, &mount_config, &card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Fallo al montar SD card.");
        vTaskDelete(NULL);
    }

    // Abrir archivo binario para escritura rápida (Append)
    FILE *f = fopen("/sdcard/vibration_log.bin", "ab");
    if (f == NULL) {
        ESP_LOGE(TAG, "Fallo al abrir archivo");
    }

    VibrationMessage_t msg;
    while (1) {
        if (xQueueReceive(xSdQueue, &msg, portMAX_DELAY) == pdTRUE) {
            if (f != NULL) {
                // Escritura binaria rápida del struct entero
                fwrite(msg.data_ptr, sizeof(ProcessedVibrationData_t), 1, f);
                // Forzar flush a SD ocasionalmente para asegurar persistencia
                static int sync_counter = 0;
                if (++sync_counter % 100 == 0) {
                    fflush(f);
                    fsync(fileno(f));
                }
            }
            // Liberar referencia al slot de pool (pool_release devuelve si ref_count == 0)
            pool_release(&msg.data_ptr);
        }
    }
}
