# Guía de inicio rápido — X4C Moscatel

## Requisitos

| Herramienta | Versión mínima |
|---|---|
| ESP-IDF | v5.x |
| Docker + Docker Compose | cualquiera |
| Node.js | v18+ |
| Git | cualquiera |

---

## 1. Configurar el firmware

Abre `codeina/train_digital_twin/main/network_task.cpp` y edita las 4 macros al inicio:

```c
#define WIFI_SSID       "MI_RED_WIFI"
#define WIFI_PASS       "MI_CONTRASEÑA"
#define MQTT_BROKER_URI "mqtt://192.168.X.X:1883"   // IP del PC donde corre Docker
#define TRAIN_ID        "T-101"
```

Para saber la IP del PC: `ipconfig` (Windows) o `ip a` (Linux/Mac).

---

## 2. Arrancar el backend Docker

```bash
cd codeina/train_digital_twin/backend
docker-compose up -d
```

Comprueba que los 4 contenedores están en "Up":

```bash
docker-compose ps
```

| Servicio | URL | Credenciales |
|---|---|---|
| InfluxDB | http://localhost:8086 | admin / adminpassword |
| Grafana | http://localhost:3000 | admin / admin |
| Mosquitto (MQTT) | localhost:1883 | sin autenticación |
| Telegraf | — | puente interno |

---

## 3. Flashear el ESP32

```bash
cd codeina/train_digital_twin
idf.py build flash monitor
```

En el monitor serie deberías ver:

```
I (xxx) MAIN: Iniciando Gemelo Digital de Tren
I (xxx) NETWORK_TASK: WiFi conectado. IP asignada: 192.168.X.X
I (xxx) NETWORK_TASK: MQTT Conectado
I (xxx) SENSOR_TASK: Dato Crudo Z: X.XX m/s2 | Freq Dominante: XX.XX Hz
```

---

## 4. Arrancar el dashboard

```bash
cd Dashboard
npm install
npm run dev
```

Abre http://localhost:5173

- Indicador **verde "En vivo · InfluxDB"** → datos reales del ESP32
- Indicador **ámbar "Simulado · Mock"** → InfluxDB no accesible (revisa paso 2)

---

## Verificar que el pipeline completo funciona

### A. InfluxDB recibe datos
1. Entra en http://localhost:8086 → `admin / adminpassword`
2. Ve a **Data Explorer** → bucket `vibrations`
3. Selecciona measurement `mqtt_consumer` → deberías ver campos: `lat`, `lon`, `accel_z`, `dominant_freq_hz`

### B. Dashboard muestra datos en el mapa
- Los marcadores aparecen en la zona Jerez–Cádiz (lat ~36.6, lon ~-6.1)
- **Verde** = vibración < 0.3 g (normal)
- **Ámbar** = vibración 0.3–0.7 g (aviso)
- **Rojo** = vibración > 0.7 g (crítico)
- Haz clic en un marcador para ver: tren, vibración, frecuencia dominante, timestamp

### C. MQTT llega al broker
```bash
# Instalar mosquitto-clients si no lo tienes
docker exec mqtt_broker mosquitto_sub -t "train/telemetry/vibrations" -v
```
Deberías ver mensajes JSON como:
```json
{"timestamp":1234567890000,"lat":36.685,"lon":-6.126,"accel_z":9.95,"dominant_freq_hz":42.5,"trainId":"T-101"}
```

---

## Formato del payload MQTT

El ESP32 publica en el topic `train/telemetry/vibrations`:

```json
{
  "timestamp":        1746700000000,
  "lat":              36.6850,
  "lon":             -6.1261,
  "alt":              55.0,
  "accel_z":          9.95,
  "dominant_freq_hz": 42.5,
  "vibration_amp":    -12.3,
  "trainId":          "T-101"
}
```

El dashboard convierte `accel_z` (m/s²) a g con `|accel_z − 9.81| / 9.81`.

> **Nota:** El ESP32 descarta automáticamente los puntos sin fix GPS para no publicar coordenadas (0, 0).

---

## Parar todo

```bash
# Backend
cd codeina/train_digital_twin/backend
docker-compose down

# Dashboard: Ctrl+C en la terminal
```

---

## Problemas frecuentes

| Síntoma | Causa probable | Solución |
|---|---|---|
| Dashboard en modo mock (ámbar) | Backend no arrancado o InfluxDB sin datos | Ejecutar paso 2, esperar 30 s y recargar |
| Monitor serie: `Conexion WiFi fallida` | SSID/contraseña incorrectos | Revisar macros en `network_task.cpp` y reflashear |
| Monitor serie: `GPS sin fix` | Módulo GPS sin señal (normal en interiores) | Llevar el dispositivo al exterior; los datos no se publican hasta obtener fix |
| `docker-compose ps` muestra contenedores en "Exit" | Puerto 8086 o 1883 ocupado por otra app | `docker-compose logs <servicio>` para ver el error |
| Mapa vacío pese a datos en InfluxDB | El query Flux filtra `-2h`; datos más antiguos no aparecen | Comprobar que el reloj del ESP32 y el host están sincronizados |
