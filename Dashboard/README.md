# Xerez4Change — Dashboard

Dashboard de monitorización de vibraciones en vía férrea.  
Visualiza en tiempo real los puntos GPS enviados por el ESP32, con código de colores por nivel de vibración, filtros por fecha/tren/anomalía y diseño en modo oscuro.

---

## Configuración y arranque

### Requisitos previos

- Node.js ≥ 18
- El backend Docker arrancado (ver sección [Backend](#backend))

### 1. Instalar dependencias

```bash
cd Xerez4Change-Dashboard
npm install
```

### 2. Configurar la conexión a InfluxDB

Edita el archivo **`.env`** en la raíz del proyecto. Los valores por defecto
corresponden a la configuración estándar del `docker-compose` del backend:

```env
VITE_INFLUX_TOKEN=my-super-secret-auth-token
VITE_INFLUX_ORG=train_org
VITE_INFLUX_BUCKET=vibrations
VITE_POLL_INTERVAL_MS=10000
```

> En desarrollo, las peticiones a InfluxDB van a través del proxy de Vite
> (`/influx → http://localhost:8086`), por lo que no hace falta exponer la URL.  
> Para producción, añade `VITE_INFLUX_URL=http://<ip-servidor>:8086`.

### 3. Arrancar el servidor de desarrollo

```bash
npm run dev
```

El dashboard estará disponible en **http://localhost:5173**.

Si InfluxDB no está arrancado, el dashboard carga automáticamente datos de
demostración (mock) y muestra el indicador **"Simulado · Mock"** en la cabecera.
En cuanto el backend esté activo y con datos, cambia a **"En vivo · InfluxDB"**.

### 4. Compilar para producción

```bash
npm run build      # genera dist/
npm run preview    # sirve el build en http://localhost:4173
```

---

## Backend

El backend vive en `X4C-Moscatel/codeina/train_digital_twin/backend/`.

### 1. Averiguar la IP de tu máquina

El ESP32 necesita saber a qué IP conectarse. Antes de flashear, obtén la IP
del host donde vas a correr Docker y anótala:

```bash
# Windows
ipconfig
# Mac / Linux
ifconfig   # o: ip route get 1 | awk '{print $7}'
```

> El ESP32 y el host deben estar en la **misma red WiFi**.

### 2. Arrancar el backend Docker

```bash
cd X4C-Moscatel/codeina/train_digital_twin/backend
docker-compose up -d
```

Levanta cuatro servicios:

| Servicio | Puerto | Descripción |
|---|---|---|
| Mosquitto | `1883` | Broker MQTT. Recibe los mensajes del ESP32 |
| InfluxDB 2.7 | `8086` | Base de datos de series temporales. Panel en http://localhost:8086 (`admin` / `adminpassword`) |
| Telegraf | — | Suscribe el topic MQTT y escribe los campos en InfluxDB |
| Grafana | `3000` | Dashboard alternativo en http://localhost:3000 (`admin` / `admin`) |

Para verificar que los contenedores están corriendo:

```bash
docker-compose ps
```

### 3. Configurar y flashear el ESP32

Antes de flashear, edita **`X4C-Moscatel/codeina/train_digital_twin/main/network_task.cpp`**
y rellena las tres macros al inicio del archivo:

```c
#define WIFI_SSID       "NOMBRE_DE_TU_RED"        // ← tu red WiFi
#define WIFI_PASS       "CONTRASEÑA_DE_TU_RED"    // ← tu contraseña
#define MQTT_BROKER_URI "mqtt://192.168.X.X:1883" // ← IP del host del paso 1
#define TRAIN_ID        "T-101"                   // ← ID único por unidad
```

Luego compila y flashea con ESP-IDF:

```bash
cd X4C-Moscatel/codeina/train_digital_twin
idf.py build flash monitor
```

Una vez el ESP32 esté corriendo y con fix GPS, los puntos empezarán a
aparecer en el mapa con el indicador **"En vivo · InfluxDB"** en la cabecera.

---

## Cómo funciona

### Flujo completo de datos

```
ESP32 (WiFi + MQTT)
    │  topic: train/telemetry/vibrations
    ▼
Mosquitto :1883
    │
    ▼
Telegraf  →  aplana JSON  →  InfluxDB :8086
                                    │
                                    │  Flux query (cada 10 s)
                                    ▼
                            geoService.ts
                                    │  parseInfluxCSV()
                                    │  accel_z → vibration_g
                                    │  deriva anomalyType
                                    ▼
                            useGeoData.ts  →  dashboardStore.ts
                                                    │
                                    ┌───────────────┘
                                    │  filteredPoints
                                    ▼
                            GeoMap.tsx  →  CircleMarker por cada punto
```

### Lógica interna capa a capa

#### `src/services/geoService.ts` — Origen de datos

Es el único punto de entrada de datos al dashboard. Ejecuta una consulta Flux
sobre InfluxDB pidiendo las últimas 2 horas de mediciones, máximo 500 puntos:

```flux
from(bucket: "vibrations")
  |> range(start: -2h)
  |> filter(fn: (r) => r._measurement == "mqtt_consumer")
  |> pivot(rowKey:["_time"], columnKey: ["_field"], valueColumn: "_value")
  |> sort(columns: ["_time"], desc: false)
  |> limit(n: 500)
```

La respuesta es un CSV anotado (formato nativo de InfluxDB v2). El parser
interno lo convierte línea a línea en objetos `GeoPoint`:

- `lat`, `lon` → coordenadas directas
- `accel_z` (m/s²) → `vibration` en g: `|accel_z − 9.81| / 9.81`
- `trainId` → identificador del tren
- `_time` → timestamp ISO
- `dominant_freq_hz` → campo opcional si el ESP32 lo calcula y envía

Si InfluxDB no responde o devuelve datos vacíos, la función cae silenciosamente
a los datos mock y el indicador de cabecera cambia de verde a ámbar.
El módulo expone `getLastSource()` para que el hook sepa qué fuente se usó.

#### `src/utils/vibration.ts` — Clasificación y colores

Toda la lógica de semáforo está centralizada aquí. Los umbrales son:

| Vibración | Nivel | Color | Radio del marcador |
|---|---|---|---|
| < 0.3 g | `ok` | Verde `#10b981` | 6 px |
| 0.3 – 0.7 g | `warning` | Ámbar `#f59e0b` | 9 px |
| ≥ 0.7 g | `critical` | Rojo `#ef4444` | 12 px |

Para ajustar cuándo se considera crítico un punto, edita las constantes
`THRESHOLDS.warning` y `THRESHOLDS.critical` en este archivo.  
El campo `level` del tipo `GeoPoint` siempre se calcula aquí; los datos
de entrada no necesitan incluirlo.

#### `src/hooks/useGeoData.ts` — Ciclo de refresco

Al montarse, lanza la primera carga (con spinner) y arranca un `setInterval`
que refresca los datos cada `VITE_POLL_INTERVAL_MS` milisegundos (10 s por defecto).
Los refrescos periódicos son silenciosos: el mapa se actualiza sin parpadear.
Al desmontar el componente, cancela el intervalo y marca las peticiones en vuelo
como descartadas para evitar actualizar estado de componentes ya desmontados.

#### `src/store/dashboardStore.ts` — Estado global (Zustand)

Almacén central con dos listas de puntos:

- `points` — todos los puntos recibidos, sin filtrar
- `filteredPoints` — resultado de aplicar los filtros activos sobre `points`

Cada vez que llegan nuevos datos (`setPoints`) o cambia un filtro (`setFilters`),
`applyFilters()` recalcula `filteredPoints` en el mismo tick. El mapa solo
consume `filteredPoints`, por lo que nunca ve datos que no pasen los filtros.

Los tres filtros disponibles se evalúan en AND:

1. **Rango de fechas** — compara el `timestamp` ISO del punto con `[inicio, fin]`
2. **ID de tren** — coincidencia exacta de `trainId`
3. **Tipo de anomalía** — coincidencia exacta de `anomalyType`

#### `src/components/GeoMap.tsx` — Visualización

Componente puramente visual. Recibe `data: GeoPoint[]` y no sabe de dónde vienen.
Renderiza sobre un `MapContainer` de Leaflet centrado en la zona Jerez–Cádiz
(`[36.62, −6.20]`, zoom 12) con capa CartoDB Dark Matter.

Por cada punto crea un `CircleMarker` cuyo color y radio vienen de
`getVibrationColor()` y `getMarkerRadius()` de `vibration.ts`. Los puntos
críticos tienen borde de 2 px en lugar de 1 px para destacar más.

Cada marcador tiene:
- **Tooltip** (hover): tren + vibración, aparece rápido
- **Popup** (click): detalle completo — nivel, tren, vibración, frecuencia dominante (si existe), tipo de anomalía (si existe) y timestamp formateado

Dos overlays posicionados con `z-index: 1000` sobre el mapa:
- **`Legend`** — abajo a la izquierda, muestra los tres colores con sus rangos
- **`Stats`** — arriba a la derecha, muestra total de puntos, avisos y críticos

#### `src/components/FilterBar.tsx` — Filtros

Los desplegables de tren y anomalía se alimentan de `fetchTrainIds()` y
`fetchAnomalyTypes()`, que derivan los valores únicos de los propios datos.
Cualquier filtro que se active llama a `setFilters()` del store, lo que
recalcula `filteredPoints` al instante sin ninguna petición de red adicional.

---

## Estructura de archivos

```
src/
├── components/
│   ├── GeoMap.tsx          # Mapa Leaflet: marcadores, leyenda, stats
│   └── FilterBar.tsx       # Filtros de fecha, tren y anomalía
├── hooks/
│   └── useGeoData.ts       # Polling + carga inicial + fuente activa
├── services/
│   └── geoService.ts       # Flux query → parse CSV → GeoPoint[] (fallback mock)
├── store/
│   └── dashboardStore.ts   # Zustand: puntos, filtros, loading, error
├── utils/
│   └── vibration.ts        # Umbrales, colores, radios, formateo
├── types.ts                # GeoPoint, DashboardFilters, VibrationLevel
├── App.tsx                 # Layout: cabecera + badge de estado + mapa
├── main.tsx                # Punto de entrada de React
└── index.css               # Tailwind + estilos oscuros para popups de Leaflet
```

---

## Referencia rápida

| Necesidad | Dónde |
|---|---|
| Cambiar token / org / bucket de InfluxDB | `.env` |
| Cambiar intervalo de refresco | `.env` → `VITE_POLL_INTERVAL_MS` |
| Cambiar IP de InfluxDB en producción | `.env` → `VITE_INFLUX_URL` |
| Ajustar umbrales de vibración | `src/utils/vibration.ts` → `THRESHOLDS` |
| Cambiar ventana de tiempo de la query | `src/services/geoService.ts` → `range(start: -2h)` |
| Cambiar centro o zoom inicial del mapa | `src/components/GeoMap.tsx` → `DEFAULT_CENTER`, `DEFAULT_ZOOM` |
| Añadir un filtro nuevo | `src/types.ts` + `src/store/dashboardStore.ts` + `src/components/FilterBar.tsx` |
