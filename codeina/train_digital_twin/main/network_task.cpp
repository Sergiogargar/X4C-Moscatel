#include "network_task.h"
#include "data_types.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "freertos/event_groups.h"
#include "nvs_flash.h"
#include "mqtt_client.h"
#include "cJSON.h"
#include <string.h>

static const char *TAG = "NETWORK_TASK";

// ─── CONFIGURAR ANTES DE FLASHEAR ────────────────────────────────
#define WIFI_SSID       "iPhone de Mibu"    // <-- pon tu SSID real aqui
#define WIFI_PASS       "xdxdxdxd"      // <-- pon tu contrasena real aqui
#define MQTT_BROKER_URI "mqtt://172.20.10.3:1883" // IP del host donde corre docker-compose
#define MQTT_TOPIC      "train/telemetry/vibrations"
#define TRAIN_ID        "T-101"  // Identificador unico de esta unidad (T-101, T-102, ...)
// ─────────────────────────────────────────────────────────────────

#define WIFI_MAXIMUM_RETRY  5

static esp_mqtt_client_handle_t client = NULL;

// Event group para sincronizar la conexion WiFi de forma bloqueante
static EventGroupHandle_t s_wifi_event_group;
static const int WIFI_CONNECTED_BIT = BIT0;
static const int WIFI_FAIL_BIT      = BIT1;
static int s_retry_num = 0;

static volatile bool s_mqtt_connected = false;

// ─── MQTT event handler ──────────────────────────────────────────
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    (void)event;
    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT Conectado");
            s_mqtt_connected = true;
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "MQTT Desconectado");
            s_mqtt_connected = false;
            break;
        default:
            break;
    }
}

// ─── WiFi event handler ──────────────────────────────────────────
// IMPORTANTE: esp_wifi_connect() debe llamarse desde aqui (evento STA_START),
// no directamente despues de esp_wifi_start(), porque el arranque es ASÍNCRONO.
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        // El driver WiFi arranco correctamente: ahora intentar conectar
        esp_wifi_connect();

    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < WIFI_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGW(TAG, "Reintentando conexion WiFi (%d/%d)...", s_retry_num, WIFI_MAXIMUM_RETRY);
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            ESP_LOGE(TAG, "Conexion WiFi fallida tras %d intentos", WIFI_MAXIMUM_RETRY);
        }

    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "WiFi conectado. IP asignada: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

// ─── Inicializacion WiFi con espera real (EventGroup) ────────────
bool wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Registrar handlers para eventos WiFi e IP
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL, NULL));

    wifi_config_t wifi_config;
    memset(&wifi_config, 0, sizeof(wifi_config));
    strncpy((char*)wifi_config.sta.ssid,     WIFI_SSID, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char*)wifi_config.sta.password, WIFI_PASS, sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    // esp_wifi_connect() sera invocado por wifi_event_handler en WIFI_EVENT_STA_START

    ESP_LOGI(TAG, "Esperando conexion WiFi a '%s'...", WIFI_SSID);
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE, pdFALSE,
                                           portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        return true;
    } else {
        ESP_LOGE(TAG, "No se pudo conectar al WiFi. Comprueba SSID y contrasena.");
        return false;
    }
}

// ─── MQTT ────────────────────────────────────────────────────────
static void mqtt_app_start(void) {
    esp_mqtt_client_config_t mqtt_cfg;
    memset(&mqtt_cfg, 0, sizeof(mqtt_cfg));
    mqtt_cfg.broker.address.uri = MQTT_BROKER_URI;

    client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(client, MQTT_EVENT_ANY, mqtt_event_handler, NULL);
    esp_mqtt_client_start(client);
}

// ─── Task principal ──────────────────────────────────────────────
void vNetworkTask(void *pvParameters) {
    bool wifi_ok = (bool)(uintptr_t)pvParameters;
    ESP_LOGI(TAG, "Inicializando Network Task en Core %d (WiFi: %s)", xPortGetCoreID(), wifi_ok ? "CONECTADO" : "OFFLINE");

    if (!wifi_ok) {
        ESP_LOGW(TAG, "Network Task iniciada en modo OFFLINE. No se publicaran mensajes en MQTT.");
        VibrationMessage_t msg;
        while (1) {
            if (xQueueReceive(xNetworkQueue, &msg, portMAX_DELAY) == pdTRUE) {
                ProcessedVibrationData_t *data = msg.data_ptr;
                // En modo offline solo liberamos la referencia al pool
                pool_release(&data);
            }
        }
        return;
    }

    mqtt_app_start();

    VibrationMessage_t msg;
    while (1) {
        if (xQueueReceive(xNetworkQueue, &msg, portMAX_DELAY) == pdTRUE) {
            ProcessedVibrationData_t *data = msg.data_ptr;

            // Sin fix GPS: usar coordenadas de prueba (Jerez) para poder ver datos en el dashboard
            // en interior. En producción con GPS exterior esto se rellena con datos reales.
            double pub_lat = data->telemetry.gps_valid ? data->telemetry.latitude  : 36.6852;
            double pub_lon = data->telemetry.gps_valid ? data->telemetry.longitude : -6.1265;
            if (!data->telemetry.gps_valid) {
                ESP_LOGW(TAG, "GPS sin fix, publicando con coordenadas de prueba (Jerez)");
            }

            if (s_mqtt_connected) {
                // Construir JSON con los campos que espera el dashboard
                cJSON *root = cJSON_CreateObject();
                cJSON_AddNumberToObject(root, "timestamp",        (double)data->telemetry.timestamp_ms);
                cJSON_AddNumberToObject(root, "lat",              pub_lat);
                cJSON_AddNumberToObject(root, "lon",              pub_lon);
                cJSON_AddNumberToObject(root, "alt",              data->telemetry.altitude);
                cJSON_AddNumberToObject(root, "accel_z",          data->telemetry.accel_z);
                cJSON_AddNumberToObject(root, "dominant_freq_hz", data->telemetry.dominant_freq_hz);
                cJSON_AddNumberToObject(root, "vibration_amp",    data->telemetry.vibration_amplitude);
                cJSON_AddStringToObject(root, "trainId",          TRAIN_ID);

                char *json_string = cJSON_PrintUnformatted(root);

                // Publicar en MQTT (QoS 0 para telemetria continua de alta frecuencia)
                esp_mqtt_client_publish(client, MQTT_TOPIC, json_string, 0, 0, 0);

                free(json_string);
                cJSON_Delete(root);
            }

            // Liberar referencia al slot de pool
            pool_release(&data);
        }
    }
}
