#include "sensor_task.h"
#include "data_types.h"
#include "esp_log.h"
#include "esp_adc/adc_continuous.h"
#include "esp_dsp.h"
#include <string.h>
#include <float.h>

static const char *TAG = "SENSOR_TASK";

#define ADC_UNIT ADC_UNIT_1
#define ADC_CHANNEL ADC_CHANNEL_0
#define SAMPLE_FREQ_HZ 10000 // 10kHz de muestreo
#define READ_LEN 1024        // Tamaño del buffer de DMA (en bytes)
#define NUM_SAMPLES_PER_FFT FFT_SAMPLES

// Arrays para la FFT
// La librería dsps_fft2r_fc32 necesita arreglos complejos donde par es real, impar es imaginario.
static float fft_input[FFT_SAMPLES * 2];
static float fft_window[FFT_SAMPLES];

// Double buffer para DMA: adc_continuous provee callbacks que usaremos para mover los datos al procesador.
// En este caso, usaremos xStreamBuffer o lecturas directas bloqueantes controladas por vTaskDelay.
// Para máxima eficiencia sin delay, usaremos el API continuo de ESP-IDF.

static bool IRAM_ATTR adc_conv_done_cb(adc_continuous_handle_t handle, const adc_continuous_evt_data_t *edata, void *user_data) {
    BaseType_t mustYield = pdFALSE;
    TaskHandle_t task = (TaskHandle_t)user_data;
    vTaskNotifyGiveFromISR(task, &mustYield);
    return (mustYield == pdTRUE);
}

void vSensorTask(void *pvParameters) {
    ESP_LOGI(TAG, "Inicializando Sensor Task en Core %d", xPortGetCoreID());

    // 1. Inicializar ESP-DSP
    esp_err_t ret = dsps_fft2r_init_fc32(NULL, CONFIG_DSP_MAX_FFT_SIZE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "No se pudo inicializar ESP-DSP");
        vTaskDelete(NULL);
    }
    // Generar ventana Hann
    dsps_wind_hann_f32(fft_window, FFT_SAMPLES);

    // 2. Configurar ADC Continuo (DMA)
    adc_continuous_handle_t adc_handle;
    adc_continuous_handle_cfg_t adc_config = {
        .max_store_buf_size = READ_LEN * 4,
        .conv_frame_size = READ_LEN,
    };
    ESP_ERROR_CHECK(adc_continuous_new_handle(&adc_config, &adc_handle));

    adc_continuous_config_t dig_cfg = {
        .sample_freq_hz = SAMPLE_FREQ_HZ,
        .conv_mode = ADC_CONV_SINGLE_UNIT_1,
        .format = ADC_DIGI_OUTPUT_FORMAT_TYPE1,
    };
    
    adc_digi_pattern_config_t adc_pattern[1] = {
        {
            .atten = ADC_ATTEN_DB_12,
            .channel = ADC_CHANNEL & 0x7,
            .unit = ADC_UNIT,
            .bit_width = SOC_ADC_DIGI_MAX_BITWIDTH,
        }
    };
    dig_cfg.pattern_num = 1;
    dig_cfg.adc_pattern = adc_pattern;
    ESP_ERROR_CHECK(adc_continuous_config(adc_handle, &dig_cfg));

    adc_continuous_evt_cbs_t cbs = {
        .on_conv_done = adc_conv_done_cb,
    };
    ESP_ERROR_CHECK(adc_continuous_register_event_callbacks(adc_handle, &cbs, xTaskGetCurrentTaskHandle()));

    ESP_ERROR_CHECK(adc_continuous_start(adc_handle));

    uint8_t rx_buffer[READ_LEN];
    uint32_t ret_num = 0;
    int samples_collected = 0;

    while (1) {
        // Esperamos a que el DMA nos notifique que hay datos (sin blockear innecesariamente CPU, cero delay())
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        while (adc_continuous_read(adc_handle, rx_buffer, READ_LEN, &ret_num, 0) == ESP_OK) {
            // Extraer las muestras
            for (int i = 0; i < ret_num; i += SOC_ADC_DIGI_RESULT_BYTES) {
                adc_digi_output_data_t *p = (adc_digi_output_data_t*)&rx_buffer[i];
                if (p->type1.channel == ADC_CHANNEL) {
                    if (samples_collected < FFT_SAMPLES) {
                        fft_input[samples_collected * 2 + 0] = (float)p->type1.data; // Parte Real
                        fft_input[samples_collected * 2 + 1] = 0;                    // Parte Imaginaria
                        samples_collected++;
                    }
                }
            }
        }

        // Si tenemos suficientes muestras, calculamos FFT
        if (samples_collected >= FFT_SAMPLES) {
            samples_collected = 0; // Reiniciar para el siguiente frame

            // Aplicar ventana
            for (int i = 0; i < FFT_SAMPLES; i++) {
                fft_input[i * 2 + 0] *= fft_window[i];
            }

            // FFT
            dsps_fft2r_fc32(fft_input, FFT_SAMPLES);
            dsps_bit_rev_fc32(fft_input, FFT_SAMPLES);

            // Obtener memoria del pool para enviar el resultado sin bloquear ni perder datos
            ProcessedVibrationData_t* pData = NULL;
            if (xQueueReceive(xVibrationDataPool, &pData, 0) == pdTRUE) {
                // Copiar la telemetría actual (Sincronizado)
                if (xSemaphoreTake(xTelemetryMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                    pData->telemetry = currentTelemetry;
                    xSemaphoreGive(xTelemetryMutex);
                }

                // Calcular magnitudes y guardarlas en el buffer
                float max_magnitude = -FLT_MAX;
                int   max_bin       = 1;
                for (int i = 0; i < FFT_RESOLUTION; i++) {
                    pData->fft_spectrum[i] = 10 * log10f((fft_input[i * 2 + 0] * fft_input[i * 2 + 0] + fft_input[i * 2 + 1] * fft_input[i * 2 + 1]) / FFT_SAMPLES);
                    // Ignorar bin 0 (componente DC) al buscar la frecuencia dominante
                    if (i > 0 && pData->fft_spectrum[i] > max_magnitude) {
                        max_magnitude = pData->fft_spectrum[i];
                        max_bin       = i;
                    }
                }
                // freq_dominante = bin × (Fs / N)
                pData->telemetry.dominant_freq_hz = (float)max_bin * ((float)SAMPLE_FREQ_HZ / (float)FFT_SAMPLES);

                VibrationMessage_t msg;
                msg.data_ptr = pData;

                // Enviar a SD Task
                if (xQueueSend(xSdQueue, &msg, 0) != pdTRUE) {
                    // Si falla, al menos no perdemos la memoria, intentamos enviar a Network
                }
                // Enviar a Network Task (usamos otro mensaje, misma referencia, Network o SD deberán liberar al final,
                // O implementar reference counting. Para este ejemplo, haremos que la Network task libere (devuelva al pool))
                if (xQueueSend(xNetworkQueue, &msg, 0) != pdTRUE) {
                    // Si Network falla también, devolvemos al pool para no perder memoria
                    xQueueSend(xVibrationDataPool, &pData, 0);
                }
            } else {
                ESP_LOGW(TAG, "Data pool vacio, perdiendo trama");
            }
        }
    }
}
