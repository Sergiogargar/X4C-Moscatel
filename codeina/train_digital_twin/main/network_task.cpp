#include "network_task.h"
#include "data_types.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "mqtt_client.h"
#include "cJSON.h"
#include <string.h>

static const char *TAG = "NETWORK_TASK";

// ─── CONFIGURAR ANTES DE FLASHEAR ────────────────────────────────
#define WIFI_SSID       "NOMBRE_DE_TU_RED"       // SSID de la red WiFi compartida
#define WIFI_PASS       "CONTRASEÑA_DE_TU_RED"   // Contraseña WiFi
#define MQTT_BROKER_URI "mqtt://192.168.1.100:1883" // IP del host donde corre docker-compose
#define MQTT_TOPIC      "train/telemetry/vibrations"
#define TRAIN_ID        "T-101"  // Identificador único de esta unidad (T-101, T-102, ...)
// ─────────────────────────────────────────────────────────────────

static esp_mqtt_client_handle_t client = NULL;

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT Conectado");
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "MQTT Desconectado");
            break;
        default:
            break;
    }
}

static void wifi_init_sta(void) {
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
        },
    };
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_start();
    esp_wifi_connect();
}

static void mqtt_app_start(void) {
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker = {
            .address = {
                .uri = MQTT_BROKER_URI
            }
        }
    };
    client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(client);
}

void vNetworkTask(void *pvParameters) {
    ESP_LOGI(TAG, "Inicializando Network Task en Core %d", xPortGetCoreID());

    // NVS requerido por Wi-Fi
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }

    wifi_init_sta();
    
    // Esperar un poco para conectar (en prod usar event bits)
    vTaskDelay(pdMS_TO_TICKS(5000));
    
    mqtt_app_start();

    VibrationMessage_t msg;
    while (1) {
        if (xQueueReceive(xNetworkQueue, &msg, portMAX_DELAY) == pdTRUE) {
            ProcessedVibrationData_t *data = msg.data_ptr;

            // Descartar puntos sin fix GPS: coordenadas (0,0) no son útiles en el dashboard
            if (!data->telemetry.gps_valid) {
                ESP_LOGW(TAG, "GPS sin fix, descartando publicacion MQTT");
                xQueueSend(xVibrationDataPool, &data, 0);
                continue;
            }

            // Construir JSON con los campos que espera el dashboard
            cJSON *root = cJSON_CreateObject();
            cJSON_AddNumberToObject(root, "timestamp",        (double)data->telemetry.timestamp_ms);
            cJSON_AddNumberToObject(root, "lat",              data->telemetry.latitude);
            cJSON_AddNumberToObject(root, "lon",              data->telemetry.longitude);
            cJSON_AddNumberToObject(root, "alt",              data->telemetry.altitude);
            cJSON_AddNumberToObject(root, "accel_z",          data->telemetry.accel_z);
            cJSON_AddNumberToObject(root, "dominant_freq_hz", data->telemetry.dominant_freq_hz);
            cJSON_AddStringToObject(root, "trainId",          TRAIN_ID);

            char *json_string = cJSON_PrintUnformatted(root);

            // Publicar en MQTT (QoS 0 para telemetría continua de alta frecuencia)
            esp_mqtt_client_publish(client, MQTT_TOPIC, json_string, 0, 0, 0);

            free(json_string);
            cJSON_Delete(root);

            // Devolver la memoria del struct al pool para reutilización
            xQueueSend(xVibrationDataPool, &data, 0);
        }
    }
}
