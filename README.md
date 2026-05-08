# X4C-Moscatel — Firmware ESP32

Nodo IoT de borde para monitorización de infraestructura ferroviaria.  
Captura la firma vibratoria del tren mediante el IMU ICM-20948, la geolocaliza
con el GPS NEO-6M y publica los datos en tiempo real vía MQTT para alimentar
el dashboard de visualización.

---

## Puesta en marcha

### Requisitos previos

- [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/get-started/) v5.x instalado y configurado
- Docker + Docker Compose (para el backend)
- El ESP32-S3 conectado por USB

---

### Paso 1 — Obtener la IP del host

El ESP32 publica los datos MQTT al host donde corre Docker.
Antes de compilar, averigua su IP (deben estar en la misma red WiFi):

```bash
# Windows
ipconfig

# Mac / Linux
ifconfig   # o: ip route get 1 | awk '{print $7}'
```

---

### Paso 2 — Configurar el firmware

Abre **`codeina/train_digital_twin/main/network_task.cpp`** y rellena las
macros al inicio del archivo:

```c
// ─── CONFIGURAR ANTES DE FLASHEAR ────────────────────────────────
#define WIFI_SSID       "NOMBRE_DE_TU_RED"        // ← SSID de tu red WiFi
#define WIFI_PASS       "CONTRASEÑA_DE_TU_RED"    // ← Contraseña WiFi
#define MQTT_BROKER_URI "mqtt://192.168.X.X:1883" // ← IP del host del paso 1
#define TRAIN_ID        "T-101"  // ← ID único por unidad (T-101, T-102, ...)
```

> Si tienes varios ESP32, cada uno debe tener un `TRAIN_ID` distinto.

---

### Paso 3 — Arrancar el backend Docker

```bash
cd codeina/train_digital_twin/backend
docker-compose up -d
```

Comprueba que los cuatro contenedores están corriendo:

```bash
docker-compose ps
```

| Servicio | Puerto | Descripción |
|---|---|---|
| Mosquitto | `1883` | Broker MQTT. Recibe los mensajes del ESP32 |
| InfluxDB 2.7 | `8086` | Base de datos. Panel en http://localhost:8086 (`admin` / `adminpassword`) |
| Telegraf | — | Lee el topic MQTT y escribe en InfluxDB |
| Grafana | `3000` | Visualización alternativa en http://localhost:3000 (`admin` / `admin`) |

---

### Paso 4 — Compilar y flashear

```bash
cd codeina/train_digital_twin
idf.py build
idf.py flash
idf.py monitor    # muestra logs serie para verificar conexión WiFi y MQTT
```

O en un solo comando:

```bash
idf.py build flash monitor
```

---

### Paso 5 — Verificar que los datos llegan

Con `idf.py monitor` activo, deberías ver en los logs:

```
I (xxxx) NETWORK_TASK: MQTT Conectado
I (xxxx) NETWORK_TASK: Inicializando Network Task en Core 0
```

Para confirmar que los datos llegan a InfluxDB, abre http://localhost:8086,
entra con `admin` / `adminpassword`, ve a **Data Explorer** y consulta el
bucket `vibrations`. Si hay filas, el pipeline completo funciona.

---

### Referencia rápida de configuración

| Qué cambiar | Archivo | Dónde |
|---|---|---|
| Red WiFi y contraseña | `main/network_task.cpp` | Macros `WIFI_SSID` / `WIFI_PASS` |
| IP del broker MQTT | `main/network_task.cpp` | Macro `MQTT_BROKER_URI` |
| ID del tren | `main/network_task.cpp` | Macro `TRAIN_ID` |
| Umbral de impacto crítico | `main/sensor_task.cpp` | Frecuencia de muestreo y ventana FFT |
| Coordenadas de simulación GPS | `main/telemetry_task.cpp` | Función `parse_nmea()` |

---

## Estructura del firmware

```
codeina/train_digital_twin/
├── main/
│   ├── main.cpp            # Punto de entrada: crea las 4 tareas FreeRTOS
│   ├── data_types.h        # Estructuras de datos compartidas entre tareas
│   ├── telemetry_task.cpp  # Core 0: lee GPS (UART) e IMU (I2C) a 100 Hz
│   ├── sensor_task.cpp     # Core 1: muestreo ADC 10 kHz + FFT 1024 muestras
│   ├── network_task.cpp    # Core 0: WiFi + MQTT → publica JSON al broker
│   └── sd_task.cpp         # Core 0: escribe registros binarios en MicroSD
└── backend/
    ├── docker-compose.yml  # Mosquitto + InfluxDB + Telegraf + Grafana
    ├── mosquitto/config/   # Config del broker MQTT (allow_anonymous)
    └── telegraf/           # telegraf.conf: suscribe MQTT → escribe InfluxDB
```

---

## Flujo de datos

```
ICM-20948 (IMU)  ──┐
                   ├──▶  sensor_task  ──▶  FFT (1024 muestras, 10 kHz)
NEO-6M (GPS)  ─────┘         │                       │
                              │                       ▼
                        telemetry_task         dominant_freq_hz
                              │                       │
                              └───────────────────────┘
                                          │
                                    ProcessedVibrationData_t
                                    (pool de memoria, zero-copy)
                                       ┌──┴──┐
                                       ▼     ▼
                                   SD Task  Network Task
                                   (.bin)       │
                                             MQTT JSON
                                                │
                                          Mosquitto :1883
                                                │
                                           Telegraf
                                                │
                                         InfluxDB :8086
                                                │
                                         Dashboard React
```

### Payload JSON publicado

Cada mensaje MQTT al topic `train/telemetry/vibrations` tiene esta forma:

```json
{
  "timestamp":        1746700000000,
  "lat":              36.6850,
  "lon":              -6.1261,
  "alt":              55.0,
  "accel_z":          10.15,
  "dominant_freq_hz": 42.5,
  "trainId":          "T-101"
}
```

El dashboard convierte `accel_z` (m/s²) a g con `|accel_z − 9.81| / 9.81`
y clasifica el punto como ok / warning / critical según los umbrales definidos en la UI.

---

## Descripción del proyecto

Este es un GEM de Gemini con información completa del proyecto:
https://gemini.google.com/gem/1v8jnuFOj4cJeROFavsBkOiZ2vt1CeKzN?usp=sharing

El proyecto consiste en un nodo **IoT de borde (Edge Computing)** diseñado para
la monitorización de la infraestructura ferroviaria. Su función es capturar la
"firma vibratoria" y la posición geográfica del tren en tiempo real para crear
un modelo digital (Gemelo Digital) que permita predecir fallos en la vía o en
el material rodante.

**Los 3 pilares del Gemelo Digital:**

- **El Objeto Físico:** El tren y la infraestructura (vías, catenarias).
- **El Modelo Virtual:** Representación matemática — modelos de fatiga de materiales, algoritmos de predicción.
- **La Conexión de Datos (aquí entramos nosotros):** El flujo constante de información desde los sensores (ESP32-S3) hacia el modelo virtual.

**Algoritmo en el ESP32-S3:**

1. **Adquisición** — Core 1 muestrea la IMU a 10 kHz. Core 0 lee GPS a 1 Hz y gestiona escritura en SD.
2. **Fusión de datos** — Cada trama FFT se vincula al último GPS conocido mediante mutex FreeRTOS.
3. **FFT** — Ventana Hann de 1024 muestras → 512 bins de frecuencia (resolución ~9.8 Hz/bin, rango 0–5 kHz).
4. **Frecuencia dominante** — Se extrae el bin de mayor energía (ignorando DC) y se envía como `dominant_freq_hz`.
5. **Escritura segura** — SD escribe en bloques cada 100 registros para evitar corrupción ante cortes de energía.
