# Backend de Monitorización de Vías (Gemelo Digital)

Este directorio contiene la infraestructura para recibir, almacenar y visualizar la telemetría del ESP32-S3.

## Despliegue Rápido
Asegúrate de tener instalado **Docker** y **Docker Compose**.
1. En esta misma carpeta, ejecuta:
   ```bash
   docker-compose up -d
   ```
2. Esto levantará:
   - **Mosquitto (Broker MQTT)**: Puerto 1883
   - **InfluxDB (Base de datos Time-Series)**: Puerto 8086
   - **Telegraf (Agente)**: Conecta Mosquitto con InfluxDB.
   - **Grafana (Dashboards)**: Puerto 3000

## Credenciales por Defecto
- **InfluxDB**: `admin` / `adminpassword` (Token: `my-super-secret-auth-token`)
- **Grafana**: `admin` / `admin`

## Configuración en Grafana y Consultas (Flux)

Para visualizar los datos en un panel **Geomap** de Grafana y destacar puntos donde la vibración (ej. el pico en Z o frecuencias de la FFT) supera un umbral anómalo:

1. Añade InfluxDB como Data Source en Grafana. Selecciona el lenguaje **Flux**.
2. URL: `http://influxdb:8086`, Organization: `train_org`, Token: `my-super-secret-auth-token`, Default Bucket: `vibrations`.

### Query Flux para el Mapa (Anomalías)
Asumiendo que Telegraf inserta la latitud (`lat`), longitud (`lon`), y la aceleración en Z (`accel_z`), podemos filtrar las aceleraciones que representen impactos severos (ej. baches en la vía).

```flux
from(bucket: "vibrations")
  |> range(start: v.timeRangeStart, stop: v.timeRangeStop)
  |> filter(fn: (r) => r["_measurement"] == "mqtt_consumer")
  |> filter(fn: (r) => r["_field"] == "lat" or r["_field"] == "lon" or r["_field"] == "accel_z")
  |> pivot(rowKey:["_time"], columnKey: ["_field"], valueColumn: "_value")
  // Filtrar solo vibraciones anómalas (ejemplo: aceleración Z > 12.0 m/s^2)
  |> filter(fn: (r) => r["accel_z"] > 12.0)
  |> keep(columns: ["_time", "lat", "lon", "accel_z"])
```

En Grafana, en las opciones del panel **Geomap**:
- Location: `Coords`
- Latitude field: `lat`
- Longitude field: `lon`
- Puedes usar `accel_z` para determinar el tamaño o color del marcador (ej. un marcador rojo intenso para picos grandes).

### Analizando la FFT
El espectro FFT se envía como un array JSON. InfluxDB (vía Telegraf) guardará cada índice de frecuencia si se configura correctamente o si se envían como métricas separadas en el ESP32. Para visualización avanzada de la FFT, se recomienda un panel **Bar Gauge** o **Time Series** mostrando la magnitud de frecuencias clave (ej. frecuencia de resonancia de los bogies).
