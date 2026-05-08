#include "telemetry_task.h"
#include "data_types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/uart.h"
#include "driver/i2c.h"
#include "esp_timer.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "TELEMETRY_TASK";

#define GPS_UART_NUM UART_NUM_1
#define TXD_PIN 17
#define RXD_PIN 16
#define RX_BUF_SIZE 1024

#define I2C_MASTER_NUM I2C_NUM_0
#define I2C_MASTER_SDA_IO 21
#define I2C_MASTER_SCL_IO 22

// Inicialización de UART para GPS
static void init_gps_uart() {
    const uart_config_t uart_config = {
        .baud_rate = 9600,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    uart_driver_install(GPS_UART_NUM, RX_BUF_SIZE * 2, 0, 0, NULL, 0);
    uart_param_config(GPS_UART_NUM, &uart_config);
    uart_set_pin(GPS_UART_NUM, TXD_PIN, RXD_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
}

// Inicialización de I2C para IMU
static void init_imu_i2c() {
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
    };
    conf.master.clk_speed = 400000;
    i2c_param_config(I2C_MASTER_NUM, &conf);
    i2c_driver_install(I2C_MASTER_NUM, conf.mode, 0, 0, 0);
}

// Parser muy básico (mockup) de NMEA para ejemplo. Un parser real usaría minmea o similar.
static void parse_nmea(const char* sentence, TelemetryData_t* tel) {
    if (strncmp(sentence, "$GPGGA", 6) == 0) {
        // En una app real, se extraen lat/lon usando strtok o sscanf.
        // Simularemos datos para mantener el ejemplo simple y estable.
        tel->latitude = 36.6850;   // Jerez de la Frontera (zona del dashboard)
        tel->longitude = -6.1261;
        tel->altitude = 55.0;
        tel->gps_valid = true;
    }
}

void vTelemetryTask(void *pvParameters) {
    ESP_LOGI(TAG, "Inicializando Telemetry Task en Core %d", xPortGetCoreID());

    init_gps_uart();
    init_imu_i2c();

    uint8_t* data = (uint8_t*) malloc(RX_BUF_SIZE + 1);

    while (1) {
        // Leemos del UART sin bloquear con delay, el timeout lo gestiona la API
        int rxBytes = uart_read_bytes(GPS_UART_NUM, data, RX_BUF_SIZE, pdMS_TO_TICKS(100));
        
        TelemetryData_t new_telemetry = {0};
        new_telemetry.timestamp_ms = esp_timer_get_time() / 1000;

        if (rxBytes > 0) {
            data[rxBytes] = 0;
            parse_nmea((char*)data, &new_telemetry);
        }

        // Leemos de la IMU (simulación de I2C read)
        // i2c_master_read_from_device(I2C_MASTER_NUM, IMU_ADDR, reg_data, len, timeout);
        new_telemetry.accel_x = ((float)rand() / RAND_MAX) * 2.0 - 1.0;
        new_telemetry.accel_y = ((float)rand() / RAND_MAX) * 2.0 - 1.0;
        new_telemetry.accel_z = 9.81 + ((float)rand() / RAND_MAX) * 0.5;
        
        // Actualizamos la estructura global protegida
        if (xSemaphoreTake(xTelemetryMutex, portMAX_DELAY) == pdTRUE) {
            // Mantener valores de GPS si no recibimos en este tick (pero sí IMU)
            if (!new_telemetry.gps_valid) {
                new_telemetry.latitude = currentTelemetry.latitude;
                new_telemetry.longitude = currentTelemetry.longitude;
                new_telemetry.altitude = currentTelemetry.altitude;
                new_telemetry.gps_valid = currentTelemetry.gps_valid;
            }
            currentTelemetry = new_telemetry;
            xSemaphoreGive(xTelemetryMutex);
        }

        // La IMU la podemos leer cada 10ms (100Hz)
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    free(data);
}
