#include "sd_task.h"
#include "data_types.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/spi_master.h"
#include <string.h>

static const char *TAG = "SD_TASK";

#define PIN_NUM_MISO 19
#define PIN_NUM_MOSI 23
#define PIN_NUM_CLK  18
#define PIN_NUM_CS   5

void vSdTask(void *pvParameters) {
    ESP_LOGI(TAG, "Inicializando SD Task en Core %d", xPortGetCoreID());

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = true,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };
    sdmmc_card_t *card;
    const char mount_point[] = "/sdcard";

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = PIN_NUM_MOSI,
        .miso_io_num = PIN_NUM_MISO,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };
    
    // Inicializar SPI bus
    esp_err_t ret = spi_bus_initialize(host.slot, &bus_cfg, SDSPI_DEFAULT_DMA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Fallo al inicializar SPI bus.");
        vTaskDelete(NULL);
    }

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = (gpio_num_t)PIN_NUM_CS;
    slot_config.host_id = host.slot;

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
                // Forzar flush a SD ocasionalmente (reduce rendimiento, pero asegura persistencia)
                static int sync_counter = 0;
                if (++sync_counter % 100 == 0) {
                    fflush(f);
                    fsync(fileno(f));
                }
            }
        }
    }
}
