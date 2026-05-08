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
#include <stdlib.h>

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

    // 1. Inicializar Primitivas de Sincronización
    xTelemetryMutex = xSemaphoreCreateMutex();
    
    // Cola para el Data Pool (Almacena punteros a memoria pre-alojada)
    xVibrationDataPool = xQueueCreate(MEMORY_POOL_SIZE, sizeof(ProcessedVibrationData_t*));
    
    // Colas de paso de mensajes (Almacenan struct VibrationMessage_t)
    xSdQueue = xQueueCreate(MEMORY_POOL_SIZE, sizeof(VibrationMessage_t));
    xNetworkQueue = xQueueCreate(MEMORY_POOL_SIZE, sizeof(VibrationMessage_t));

    if (xVibrationDataPool == NULL || xSdQueue == NULL || xNetworkQueue == NULL || xTelemetryMutex == NULL) {
        ESP_LOGE(TAG, "Fallo al crear colas o semáforos. Memoria insuficiente.");
        return;
    }

    // 2. Pre-alojar memoria para el Memory Pool
    for (int i = 0; i < MEMORY_POOL_SIZE; i++) {
        // Alocar en memoria interna o PSRAM
        ProcessedVibrationData_t *pData = (ProcessedVibrationData_t *)malloc(sizeof(ProcessedVibrationData_t));
        if (pData != NULL) {
            xQueueSend(xVibrationDataPool, &pData, portMAX_DELAY);
        } else {
            ESP_LOGE(TAG, "Fallo al alocar memoria para el pool");
            return;
        }
    }

    // 3. Crear Tareas y asignarlas a los Cores adecuados (Core Pinning)
    
    // Core 0: Tareas de Red, I/O, y Telemetría Asíncrona
    xTaskCreatePinnedToCore(
        vTelemetryTask,
        "TelemetryTask",
        4096,
        NULL,
        1,      // Prioridad baja
        NULL,
        0       // Core 0
    );

    xTaskCreatePinnedToCore(
        vSdTask,
        "SdTask",
        4096,
        NULL,
        2,      // Prioridad media
        NULL,
        0       // Core 0
    );

    xTaskCreatePinnedToCore(
        vNetworkTask,
        "NetworkTask",
        8192,
        NULL,
        3,      // Prioridad alta en Core 0
        NULL,
        0       // Core 0
    );

    // Core 1: Tareas de Tiempo Crítico (DMA / Procesamiento DSP FFT)
    xTaskCreatePinnedToCore(
        vSensorTask,
        "SensorTask",
        8192,
        NULL,
        configMAX_PRIORITIES - 1, // Prioridad máxima (Tiempo Crítico)
        NULL,
        1                         // Core 1
    );

    ESP_LOGI(TAG, "Todas las tareas iniciadas correctamente.");
}
