#ifndef DATA_TYPES_H
#define DATA_TYPES_H

#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

// Definiciones de la FFT y muestreo
#define FFT_SAMPLES 1024
#define FFT_RESOLUTION (FFT_SAMPLES / 2)

// Estructura para telemetría continua (GPS e IMU)
typedef struct {
    uint64_t timestamp_ms; // Unix timestamp en ms o uptime del sistema
    double latitude;
    double longitude;
    float altitude;
    float accel_x;
    float accel_y;
    float accel_z;
    float gyro_x;
    float gyro_y;
    float gyro_z;
    bool gps_valid;
    float dominant_freq_hz; // Frecuencia de mayor energía en el espectro FFT (Hz)
} TelemetryData_t;

// Estructura con datos listos para enviar a SD y Red
typedef struct {
    TelemetryData_t telemetry;       // Estado del tren en el momento del muestreo
    float fft_spectrum[FFT_RESOLUTION]; // Espectro de frecuencias (magnitudes)
} ProcessedVibrationData_t;

// Elemento de cola. Usamos puntero para evitar copiar todo el array grande (Zero-Copy)
typedef struct {
    ProcessedVibrationData_t* data_ptr;
} VibrationMessage_t;

// --- Variables y Handles Globales de Sincronización ---

// Mutex para proteger la lectura/escritura de la telemetría actual
extern SemaphoreHandle_t xTelemetryMutex;

// Estructura global que mantiene la última telemetría leída
extern TelemetryData_t currentTelemetry;

// Colas de FreeRTOS para enrutar el dato procesado
extern QueueHandle_t xSdQueue;
extern QueueHandle_t xNetworkQueue;

// Pool de memoria para evitar mallocs y fragmentación (Memory Pool para ProcessedVibrationData_t)
// Se gestionarán en sensor_task
extern QueueHandle_t xVibrationDataPool;

#endif // DATA_TYPES_H
