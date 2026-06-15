#define CONFIG_I2C_SUPPRESS_DEPRECATE_WARN 1
#include "sensor_task.h"
#include "data_types.h"
#include "esp_log.h"
#include "esp_dsp.h"
#include "driver/i2c.h"
#include "driver/gptimer.h"
#include <string.h>
#include <float.h>
#include <math.h>

static const char *TAG = "SENSOR_TASK";

#define SAMPLE_FREQ_HZ 1000 // 1 kHz para el ICM-20948
#define NUM_SAMPLES_PER_FFT FFT_SAMPLES

#define I2C_MASTER_NUM I2C_NUM_0
#define I2C_MASTER_SDA_IO 8
#define I2C_MASTER_SCL_IO 9

static uint8_t s_mpu6050_addr = 0x68;

// === REGISTROS MPU6050 ===
#define MPU6050_ACCEL_XOUT_H 0x3B

static float fft_input[FFT_SAMPLES * 2];
static float fft_window[FFT_SAMPLES];

static esp_err_t mpu6050_write_reg(uint8_t reg, uint8_t data) {
    uint8_t write_buf[2] = {reg, data};
    esp_err_t err = i2c_master_write_to_device(I2C_MASTER_NUM, s_mpu6050_addr, write_buf, sizeof(write_buf), pdMS_TO_TICKS(100));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error escribiendo 0x%02X en registro 0x%02X (I2C error %d)", data, reg, err);
    }
    return err;
}

static esp_err_t mpu6050_read_reg(uint8_t reg, uint8_t *data, size_t len) {
    esp_err_t err = i2c_master_write_read_device(I2C_MASTER_NUM, s_mpu6050_addr, &reg, 1, data, len, pdMS_TO_TICKS(100));
    if (err != ESP_OK) {
        static int fail_count = 0;
        if (fail_count++ % 1000 == 0) ESP_LOGE(TAG, "Error leyendo registro 0x%02X (I2C error %d)", reg, err);
    }
    return err;
}

// Escanea el bus I2C enviando solo el byte de dirección (write de 1 byte a reg 0x00).
// El driver legacy de ESP-IDF no acepta read_size=0, por eso se hace write puro.
// Si el dispositivo ACKa → está presente. Si NACKa → ESP_ERR_TIMEOUT o ESP_FAIL.
static void i2c_scan() {
    ESP_LOGW(TAG, "=== ESCANER I2C (SDA=GPIO%d, SCL=GPIO%d) ===", I2C_MASTER_SDA_IO, I2C_MASTER_SCL_IO);
    int found = 0;
    for (uint8_t addr = 0x08; addr < 0x78; addr++) {
        uint8_t probe = 0x00; // pedimos registro 0x00 sin leer nada después
        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
        i2c_master_write_byte(cmd, probe, true);
        i2c_master_stop(cmd);
        esp_err_t ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, pdMS_TO_TICKS(10));
        i2c_cmd_link_delete(cmd);
        if (ret == ESP_OK) {
            ESP_LOGW(TAG, "  Dispositivo encontrado en 0x%02X", addr);
            found++;
        }
    }
    if (found == 0) {
        ESP_LOGE(TAG, "  NINGUN dispositivo I2C encontrado. Revisa cableado SDA(GPIO%d)/SCL(GPIO%d) y VCC(3.3V).",
                 I2C_MASTER_SDA_IO, I2C_MASTER_SCL_IO);
    }
    ESP_LOGW(TAG, "=== FIN ESCANER I2C (%d dispositivos) ===", found);
}

static void mpu6050_configure(void) {
    // Salir del modo sleep sin DEVICE_RESET (más seguro para clones)
    // 0x00 = oscilador interno, sleep=0, todos los sistemas activos
    mpu6050_write_reg(0x6B, 0x00);
    vTaskDelay(pdMS_TO_TICKS(50));

    // Deshabilitar FIFO y DMP (User Control)
    mpu6050_write_reg(0x6A, 0x00);
    vTaskDelay(pdMS_TO_TICKS(10));

    // Rango acelerómetro: ±2g → divisor 16384 LSB/g
    mpu6050_write_reg(0x1C, 0x00);
    vTaskDelay(pdMS_TO_TICKS(10));

    // Rango giroscopio: ±250 °/s → divisor 131 LSB/°/s
    mpu6050_write_reg(0x1B, 0x00);
    vTaskDelay(pdMS_TO_TICKS(10));

    // Habilitar todos los ejes (PWR_MGMT_2 = 0x00)
    mpu6050_write_reg(0x6C, 0x00);
    vTaskDelay(pdMS_TO_TICKS(10));
}

static void init_imu_i2c() {
    i2c_config_t conf;
    memset(&conf, 0, sizeof(conf));
    conf.mode = I2C_MODE_MASTER;
    conf.sda_io_num = (gpio_num_t)I2C_MASTER_SDA_IO;
    conf.scl_io_num = (gpio_num_t)I2C_MASTER_SCL_IO;
    conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
    conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
    conf.master.clk_speed = 400000;
    i2c_param_config(I2C_MASTER_NUM, &conf);
    i2c_driver_install(I2C_MASTER_NUM, conf.mode, 0, 0, 0);

    i2c_scan();

    ESP_LOGI(TAG, "Iniciando inicialización IMU...");

    // Configurar en dirección por defecto y leer WHO_AM_I
    mpu6050_configure();
    uint8_t who = 0;
    mpu6050_read_reg(0x75, &who, 1);
    ESP_LOGW(TAG, "WHO_AM_I en 0x%02X = 0x%02X (MPU6050=0x68/0x70, ICM-20948=0xEA)", s_mpu6050_addr, who);

    // Si no hay respuesta útil, probar en 0x69 (AD0=VCC)
    if (who == 0x00) {
        ESP_LOGW(TAG, "Sin respuesta en 0x68, probando 0x69...");
        s_mpu6050_addr = 0x69;
        mpu6050_configure();
        mpu6050_read_reg(0x75, &who, 1);
        ESP_LOGW(TAG, "WHO_AM_I en 0x69 = 0x%02X", who);
        if (who == 0x00) {
            s_mpu6050_addr = 0x68; // restaurar por defecto
            ESP_LOGE(TAG, "Sensor IMU NO encontrado en 0x68 ni 0x69. Revisa SDA/SCL.");
        }
    }

    ESP_LOGI(TAG, "IMU listo en dirección 0x%02X", s_mpu6050_addr);

    // Dump de registros clave para diagnóstico de chip desconocido
    uint8_t regs[8];
    uint8_t start_reg = 0x6B;
    if (mpu6050_read_reg(start_reg, regs, 8) == ESP_OK) {
        ESP_LOGW(TAG, "REG DUMP 0x6B-0x72: %02X %02X %02X %02X %02X %02X %02X %02X",
                 regs[0], regs[1], regs[2], regs[3], regs[4], regs[5], regs[6], regs[7]);
    }
    uint8_t accel_regs[14];
    if (mpu6050_read_reg(0x3B, accel_regs, 14) == ESP_OK) {
        ESP_LOGW(TAG, "REG DUMP 0x3B-0x48 (accel/temp/gyro raw): "
                 "%02X%02X %02X%02X %02X%02X | %02X%02X | %02X%02X %02X%02X %02X%02X",
                 accel_regs[0], accel_regs[1],
                 accel_regs[2], accel_regs[3],
                 accel_regs[4], accel_regs[5],
                 accel_regs[6], accel_regs[7],
                 accel_regs[8], accel_regs[9],
                 accel_regs[10], accel_regs[11],
                 accel_regs[12], accel_regs[13]);
    }
}

static bool IRAM_ATTR timer_isr_handler(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *user_ctx) {
    TaskHandle_t task = (TaskHandle_t)user_ctx;
    BaseType_t high_task_wakeup = pdFALSE;
    vTaskNotifyGiveFromISR(task, &high_task_wakeup);
    return high_task_wakeup == pdTRUE;
}

void vSensorTask(void *pvParameters) {
    ESP_LOGI(TAG, "Inicializando Sensor Task en Core 1");

    init_imu_i2c();

    gptimer_handle_t gptimer = NULL;
    gptimer_config_t timer_config = {};
    timer_config.clk_src = GPTIMER_CLK_SRC_DEFAULT;
    timer_config.direction = GPTIMER_COUNT_UP;
    timer_config.resolution_hz = 1000000; // 1MHz
    timer_config.intr_priority = 0;
    timer_config.flags.intr_shared = 0;
    gptimer_new_timer(&timer_config, &gptimer);

    gptimer_alarm_config_t alarm_config = {};
    alarm_config.alarm_count = 1000; // 1000us = 1ms = 1kHz
    alarm_config.reload_count = 0;
    alarm_config.flags.auto_reload_on_alarm = true;
    gptimer_set_alarm_action(gptimer, &alarm_config);

    gptimer_event_callbacks_t cbs = {};
    cbs.on_alarm = timer_isr_handler;
    gptimer_register_event_callbacks(gptimer, &cbs, (void*) xTaskGetCurrentTaskHandle());

    esp_err_t ret = dsps_fft2r_init_fc32(NULL, CONFIG_DSP_MAX_FFT_SIZE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "No se pudo inicializar ESP-DSP");
        vTaskDelete(NULL);
    }
    
    dsps_wind_hann_f32(fft_window, FFT_SAMPLES);
    
    gptimer_enable(gptimer);
    gptimer_start(gptimer);

    int samples_collected = 0;
    int print_temp_counter = 0;

    while (1) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(10));

        uint8_t sensor_data[14];
        memset(sensor_data, 0, sizeof(sensor_data));
        bool read_ok = (mpu6050_read_reg(MPU6050_ACCEL_XOUT_H, sensor_data, 14) == ESP_OK);

        int16_t raw_ax   = (sensor_data[0] << 8) | sensor_data[1];
        int16_t raw_ay   = (sensor_data[2] << 8) | sensor_data[3];
        int16_t raw_az   = (sensor_data[4] << 8) | sensor_data[5];
        int16_t raw_temp = (sensor_data[6] << 8) | sensor_data[7];
        int16_t raw_gx   = (sensor_data[8] << 8) | sensor_data[9];
        int16_t raw_gy   = (sensor_data[10] << 8) | sensor_data[11];
        int16_t raw_gz   = (sensor_data[12] << 8) | sensor_data[13];

        if (print_temp_counter++ % 1000 == 0) {
            ESP_LOGW(TAG, "Hardware Test -> Temp Cruda: %d | Accel Z Crudo: %d | I2C: %s",
                     raw_temp, raw_az, read_ok ? "OK" : "FALLO");
        }

        float ax = ((float)raw_ax / 16384.0f) * 9.81f;
        float ay = ((float)raw_ay / 16384.0f) * 9.81f;
        float az = ((float)raw_az / 16384.0f) * 9.81f;

        if (xSemaphoreTake(xTelemetryMutex, 0) == pdTRUE) {
            currentTelemetry.accel_x = ax;
            currentTelemetry.accel_y = ay;
            currentTelemetry.accel_z = az;
            currentTelemetry.gyro_x = (float)raw_gx / 131.0f;
            currentTelemetry.gyro_y = (float)raw_gy / 131.0f;
            currentTelemetry.gyro_z = (float)raw_gz / 131.0f;
            xSemaphoreGive(xTelemetryMutex);
        }

        // Acumular muestra siempre (con ceros si I2C falló) para no detener la FFT
        if (samples_collected < FFT_SAMPLES) {
            fft_input[samples_collected * 2 + 0] = az;
            fft_input[samples_collected * 2 + 1] = 0;
            samples_collected++;
        }

        if (samples_collected >= FFT_SAMPLES) {
            samples_collected = 0;

            // 1. Eliminar el Offset DC (La gravedad constante)
            float sum = 0.0f;
            for (int i = 0; i < FFT_SAMPLES; i++) {
                sum += fft_input[i * 2 + 0];
            }
            float mean = sum / FFT_SAMPLES;

            // 2. Restar la media y aplicar ventana de Hann
            for (int i = 0; i < FFT_SAMPLES; i++) {
                fft_input[i * 2 + 0] -= mean;
                fft_input[i * 2 + 0] *= fft_window[i];
            }

            dsps_fft2r_fc32(fft_input, FFT_SAMPLES);
            dsps_bit_rev_fc32(fft_input, FFT_SAMPLES);

            ProcessedVibrationData_t* pData = NULL;
            if (xQueueReceive(xVibrationDataPool, &pData, 0) == pdTRUE) {
                if (xSemaphoreTake(xTelemetryMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                    pData->telemetry = currentTelemetry;
                    xSemaphoreGive(xTelemetryMutex);
                }

                float max_magnitude = -FLT_MAX;
                int   max_bin       = 1;
                // Empezamos en i=1 porque el bin 0 es DC (que ya debería ser cero por restar la media)
                for (int i = 1; i < FFT_RESOLUTION; i++) {
                    float energy = (fft_input[i * 2 + 0] * fft_input[i * 2 + 0] + fft_input[i * 2 + 1] * fft_input[i * 2 + 1]) / FFT_SAMPLES;
                    if (energy < 1e-10f) energy = 1e-10f; // Evitar log10(0) que da -infinito
                    pData->fft_spectrum[i] = 10 * log10f(energy);
                    
                    if (pData->fft_spectrum[i] > max_magnitude) {
                        max_magnitude = pData->fft_spectrum[i];
                        max_bin       = i;
                    }
                }
                
                pData->telemetry.dominant_freq_hz = (float)max_bin * ((float)SAMPLE_FREQ_HZ / (float)FFT_SAMPLES);
                pData->telemetry.vibration_amplitude = max_magnitude;
                
                // Actualizar la estructura global para que MAIN_DEP la imprima correctamente
                if (xSemaphoreTake(xTelemetryMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                    currentTelemetry.dominant_freq_hz = pData->telemetry.dominant_freq_hz;
                    currentTelemetry.vibration_amplitude = pData->telemetry.vibration_amplitude;
                    xSemaphoreGive(xTelemetryMutex);
                }

                ESP_LOGI(TAG, "Dato Crudo Z: %.2f m/s2 | Freq Dominante: %.2f Hz | Amplitud: %.2f dB", 
                         currentTelemetry.accel_z, pData->telemetry.dominant_freq_hz, pData->telemetry.vibration_amplitude);

                VibrationMessage_t msg;
                msg.data_ptr = pData;

                // ref_count debe estar en 2 antes de cualquier send, para que
                // los consumidores no liberen el slot prematuramente si procesan
                // más rápido de lo que este core termina de ajustar el contador.
                pData->ref_count = 2;

                bool sd_sent  = (xQueueSend(xSdQueue,      &msg, 0) == pdTRUE);
                bool net_sent = (xQueueSend(xNetworkQueue, &msg, 0) == pdTRUE);

                // Compensar referencias de consumidores que no recibieron el mensaje
                if (!sd_sent)  pool_release(&pData);
                if (!net_sent) pool_release(&pData);

                if (!sd_sent && !net_sent) {
                    ESP_LOGW(TAG, "Ambas colas llenas, trama descartada");
                }
            } else {
                ESP_LOGW(TAG, "Data pool vacio, perdiendo trama");
            }
        }
    }
}
