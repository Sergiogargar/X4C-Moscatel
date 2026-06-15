#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "data_types.h"
#include "sensor_task.h"
#include "telemetry_task.h"
#include "sd_task.h"
#include "network_task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "MAIN";

// Definición de variables globales
SemaphoreHandle_t xTelemetryMutex;
TelemetryData_t currentTelemetry;

QueueHandle_t xSdQueue;
QueueHandle_t xNetworkQueue;
QueueHandle_t xVibrationDataPool;

#define MEMORY_POOL_SIZE 10

extern "C" void app_main() {
    ESP_LOGI(TAG, "Iniciando Gemelo Digital de Tren (Vibraciones)");

    // 1. Inicializar NVS (requerido para WiFi)
    esp_err_t nvs_ret = nvs_flash_init();
    if (nvs_ret == ESP_ERR_NVS_NO_FREE_PAGES || nvs_ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs_ret);

    // 2. Inicializar stack TCP/IP y bucle de eventos (requerido para WiFi)
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // 3. Conectar WiFi antes de crear las tareas
    bool wifi_ok = wifi_init_sta();
    if (!wifi_ok) {
        ESP_LOGW(TAG, "Sin conexion WiFi: el sistema arranca en modo offline (solo SD).");
    }

    // 4. Inicializar Primitivas de Sincronización
    memset(&currentTelemetry, 0, sizeof(currentTelemetry));
    xTelemetryMutex = xSemaphoreCreateMutex();

    xVibrationDataPool = xQueueCreate(MEMORY_POOL_SIZE, sizeof(ProcessedVibrationData_t*));
    xSdQueue      = xQueueCreate(MEMORY_POOL_SIZE, sizeof(VibrationMessage_t));
    xNetworkQueue = xQueueCreate(MEMORY_POOL_SIZE, sizeof(VibrationMessage_t));

    if (xVibrationDataPool == NULL || xSdQueue == NULL || xNetworkQueue == NULL || xTelemetryMutex == NULL) {
        ESP_LOGE(TAG, "Fallo al crear colas o semáforos. Memoria insuficiente.");
        return;
    }

    // 5. Pre-alojar memoria para el Memory Pool
    for (int i = 0; i < MEMORY_POOL_SIZE; i++) {
        ProcessedVibrationData_t *pData = (ProcessedVibrationData_t *)malloc(sizeof(ProcessedVibrationData_t));
        if (pData != NULL) {
            pData->ref_count = 0;
            xQueueSend(xVibrationDataPool, &pData, portMAX_DELAY);
        } else {
            ESP_LOGE(TAG, "Fallo al alocar memoria para el pool");
            return;
        }
    }

    // 6. Crear Tareas y asignarlas a los Cores adecuados (Core Pinning)

    // Core 0: Red, I/O, Telemetría
    xTaskCreatePinnedToCore(vTelemetryTask, "TelemetryTask", 4096, NULL, 1, NULL, 0);
    xTaskCreatePinnedToCore(vSdTask,        "SdTask",        4096, NULL, 2, NULL, 0);

    // wifi_ok se pasa a NetworkTask para que sepa si MQTT está disponible
    xTaskCreatePinnedToCore(vNetworkTask, "NetworkTask", 8192, (void*)(uintptr_t)wifi_ok, 3, NULL, 0);

    // Core 1: Sensor + FFT (tiempo crítico)
    xTaskCreatePinnedToCore(vSensorTask, "SensorTask", 8192, NULL, configMAX_PRIORITIES - 1, NULL, 1);

    ESP_LOGI(TAG, "Todas las tareas iniciadas correctamente.");
}
