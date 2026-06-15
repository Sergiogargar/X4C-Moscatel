#define CONFIG_I2C_SUPPRESS_DEPRECATE_WARN 1
#include "telemetry_task.h"
#include "data_types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/uart.h"
#include "esp_timer.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>

static const char *TAG = "TELEMETRY_TASK";

#define GPS_UART_NUM UART_NUM_1
#define TXD_PIN 17
#define RXD_PIN 18
#define RX_BUF_SIZE 1024

// Inicialización de UART para GPS
static void init_gps_uart() {
    uart_config_t uart_config;
    memset(&uart_config, 0, sizeof(uart_config));
    uart_config.baud_rate = 9600;
    uart_config.data_bits = UART_DATA_8_BITS;
    uart_config.parity = UART_PARITY_DISABLE;
    uart_config.stop_bits = UART_STOP_BITS_1;
    uart_config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    uart_config.source_clk = UART_SCLK_DEFAULT;
    uart_driver_install(GPS_UART_NUM, RX_BUF_SIZE * 2, 0, 0, NULL, 0);
    uart_param_config(GPS_UART_NUM, &uart_config);
    uart_set_pin(GPS_UART_NUM, TXD_PIN, RXD_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
}

static void parse_nmea(const char* buffer, TelemetryData_t* tel) {
    // Soporte para $GPGGA (GPS puro) y $GNGGA (GNSS multi-constelación)
    const char* sentence = strstr(buffer, "GPGGA,");
    if (!sentence) sentence = strstr(buffer, "GNGGA,");
    if (sentence == NULL) {
        if (strlen(buffer) > 5) {
            ESP_LOGD(TAG, "NMEA sin sentencia GGA: %.40s", buffer);
        }
        return;
    }

    float lat = 0.0f, lon = 0.0f, alt = 0.0f;
    char lat_dir = 0, lon_dir = 0;
    int fix_quality = 0;

    // Parsear desde el identificador: "xPGGA,time,lat,dir,lon,dir,quality,..."
    // %*[^,] salta un campo hasta la coma
    if (sscanf(sentence, "%*[^,],%*[^,],%f,%c,%f,%c,%d,%*[^,],%*[^,],%f",
               &lat, &lat_dir, &lon, &lon_dir, &fix_quality, &alt) >= 5) {
        if (fix_quality > 0) {
            tel->latitude  = (int)(lat / 100) + fmod(lat, 100.0) / 60.0;
            if (lat_dir == 'S') tel->latitude  = -tel->latitude;

            tel->longitude = (int)(lon / 100) + fmod(lon, 100.0) / 60.0;
            if (lon_dir == 'W') tel->longitude = -tel->longitude;

            tel->altitude  = alt;
            tel->gps_valid = true;
            ESP_LOGI(TAG, "GPS FIJADO: Lat: %.6f, Lon: %.6f, Alt: %.1f m",
                     tel->latitude, tel->longitude, tel->altitude);
        } else {
            ESP_LOGD(TAG, "GGA recibido sin fix (quality=%d)", fix_quality);
        }
    }
}

void vTelemetryTask(void *pvParameters) {
    ESP_LOGI(TAG, "Inicializando Telemetry Task en Core %d", xPortGetCoreID());

    init_gps_uart();
    uint8_t* data = (uint8_t*) malloc(RX_BUF_SIZE + 1);

    while (1) {
        int rxBytes = uart_read_bytes(GPS_UART_NUM, data, RX_BUF_SIZE, pdMS_TO_TICKS(500));
        
        TelemetryData_t new_telemetry;
        memset(&new_telemetry, 0, sizeof(new_telemetry));
        new_telemetry.timestamp_ms = esp_timer_get_time() / 1000;

        if (rxBytes > 0) {
            data[rxBytes] = 0;
            ESP_LOGD(TAG, "UART GPS recibio %d bytes: %.80s", rxBytes, (char*)data);
            parse_nmea((char*)data, &new_telemetry);
        } else {
            static int no_data_count = 0;
            if (++no_data_count % 20 == 0) {
                ESP_LOGW(TAG, "GPS: sin datos UART tras %d ciclos (RXD=GPIO%d)", no_data_count, RXD_PIN);
            }
        }

        if (xSemaphoreTake(xTelemetryMutex, portMAX_DELAY) == pdTRUE) {
            if (new_telemetry.gps_valid) {
                currentTelemetry.latitude = new_telemetry.latitude;
                currentTelemetry.longitude = new_telemetry.longitude;
                currentTelemetry.altitude = new_telemetry.altitude;
                currentTelemetry.gps_valid = true;
            }
            currentTelemetry.timestamp_ms = new_telemetry.timestamp_ms;
            xSemaphoreGive(xTelemetryMutex);
        }
    }
    free(data);
}
