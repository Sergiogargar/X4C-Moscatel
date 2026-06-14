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
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"
#include <stdlib.h>

static const char *TAG = "MAIN_DEP";

// Definición de variables globales
SemaphoreHandle_t xTelemetryMutex;
TelemetryData_t currentTelemetry;

QueueHandle_t xSdQueue;
QueueHandle_t xNetworkQueue;
QueueHandle_t xVibrationDataPool;

#define MEMORY_POOL_SIZE 10

// Handles de las tareas para poder ver su estado
TaskHandle_t hTelemetryTask = NULL;
TaskHandle_t hSdTask = NULL;
TaskHandle_t hNetworkTask = NULL;
TaskHandle_t hSensorTask = NULL;

// Tarea extra de depuración
void vDebugTask(void *pvParameters) {
    while(1) {
        ESP_LOGW(TAG, "--- ESTADO DEL SISTEMA (DEBUG) ---");
        
        // 1. Memoria Heap
        size_t free_heap = esp_get_free_heap_size();
        size_t min_free_heap = esp_get_minimum_free_heap_size();
        ESP_LOGI(TAG, "Heap Libre: %zu bytes | Min Heap Libre Histórico: %zu bytes", free_heap, min_free_heap);

        // 2. Estado de las Colas (Muestra los espacios LIBRES)
        if (xVibrationDataPool) ESP_LOGI(TAG, "Queue DataPool (Espacios Libres): %d / %d", uxQueueSpacesAvailable(xVibrationDataPool), MEMORY_POOL_SIZE);
        if (xSdQueue) ESP_LOGI(TAG, "Queue SD (Espacios Libres): %d / %d", uxQueueSpacesAvailable(xSdQueue), MEMORY_POOL_SIZE);
        if (xNetworkQueue) ESP_LOGI(TAG, "Queue Network (Espacios Libres): %d / %d", uxQueueSpacesAvailable(xNetworkQueue), MEMORY_POOL_SIZE);

        // 3. Telemetría actual
        if (xTelemetryMutex && xSemaphoreTake(xTelemetryMutex, pdMS_TO_TICKS(100))) {
            ESP_LOGI(TAG, "Telemetría -> Freq. Dominante: %.2f Hz | Amplitud: %.2f dB | Altitud: %.2f m", 
                currentTelemetry.dominant_freq_hz, currentTelemetry.vibration_amplitude, currentTelemetry.altitude);
            xSemaphoreGive(xTelemetryMutex);
        }

        ESP_LOGW(TAG, "----------------------------------");
        vTaskDelay(pdMS_TO_TICKS(5000)); // Mostrar cada 5 segundos
    }
}

extern "C" void app_main() {
    ESP_LOGW(TAG, "Iniciando Gemelo Digital en MODO DEPURACIÓN (MAIN_DEP)");

    // 1. Inicializar Servicios Core de forma síncrona en app_main
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_LOGI(TAG, "Servicios del sistema (NVS, Netif, Event Loop) inicializados.");

    // 2. Inicializar Primitivas de Sincronización y Colas
    xTelemetryMutex = xSemaphoreCreateMutex();
    xVibrationDataPool = xQueueCreate(MEMORY_POOL_SIZE, sizeof(ProcessedVibrationData_t*));
    xSdQueue = xQueueCreate(MEMORY_POOL_SIZE, sizeof(VibrationMessage_t));
    xNetworkQueue = xQueueCreate(MEMORY_POOL_SIZE, sizeof(VibrationMessage_t));

    if (xVibrationDataPool == NULL || xSdQueue == NULL || xNetworkQueue == NULL || xTelemetryMutex == NULL) {
        ESP_LOGE(TAG, "Fallo al crear colas o semáforos. Memoria insuficiente.");
        return;
    }

    ESP_LOGI(TAG, "Colas y semáforos creados correctamente.");

    // 3. Pre-alojar memoria para el Memory Pool
    for (int i = 0; i < MEMORY_POOL_SIZE; i++) {
        ProcessedVibrationData_t *pData = (ProcessedVibrationData_t *)malloc(sizeof(ProcessedVibrationData_t));
        if (pData != NULL) {
            xQueueSend(xVibrationDataPool, &pData, portMAX_DELAY);
        } else {
            ESP_LOGE(TAG, "Fallo al alocar memoria para el pool");
            return;
        }
    }
    ESP_LOGI(TAG, "Memory pool inicializado con %d elementos.", MEMORY_POOL_SIZE);

    // 4. Crear Tarea de Depuración
    xTaskCreatePinnedToCore(
        vDebugTask,
        "DebugTask",
        4096,
        NULL,
        1,
        NULL,
        0
    );

    // 5. Inicializar Wi-Fi de forma aislada y esperar conexión ANTES de lanzar otras tareas
    // Esto evita interferencias de ruido en la radio RF causadas por el ADC continuo (DMA) o el SPI
    ESP_LOGI(TAG, "Iniciando Wi-Fi y esperando conexión...");
    bool wifi_ok = wifi_init_sta();
    
    if (wifi_ok) {
        ESP_LOGI(TAG, "Wi-Fi CONECTADO con éxito. Iniciando tareas en modo ONLINE.");
    } else {
        ESP_LOGE(TAG, "Fallo al conectar Wi-Fi. Iniciando tareas en modo OFFLINE.");
    }

    // 6. Lanzar las demás tareas ahora que Wi-Fi ya completó su inicialización y calibración
    ESP_LOGI(TAG, "Iniciando TelemetryTask...");
    xTaskCreatePinnedToCore(vTelemetryTask, "TelemetryTask", 4096, NULL, 1, &hTelemetryTask, 0);
    vTaskDelay(pdMS_TO_TICKS(100));

    ESP_LOGI(TAG, "Iniciando SdTask...");
    xTaskCreatePinnedToCore(vSdTask, "SdTask", 4096, NULL, 2, &hSdTask, 0);
    vTaskDelay(pdMS_TO_TICKS(100));

    ESP_LOGI(TAG, "Iniciando NetworkTask...");
    xTaskCreatePinnedToCore(vNetworkTask, "NetworkTask", 8192, (void*)(uintptr_t)wifi_ok, 3, &hNetworkTask, 0);
    vTaskDelay(pdMS_TO_TICKS(100));

    // Lanzar SensorTask (Tiempo Crítico / ADC continuo)
    ESP_LOGI(TAG, "Iniciando SensorTask (Tiempo Crítico)...");
    xTaskCreatePinnedToCore(vSensorTask, "SensorTask", 8192, NULL, configMAX_PRIORITIES - 1, &hSensorTask, 1);

    ESP_LOGI(TAG, "Secuencia de arranque completada.");
}
