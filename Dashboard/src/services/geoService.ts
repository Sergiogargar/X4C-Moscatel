// ─── Servicio de coordenadas GPS ─────────────────────────────────
// Intenta obtener datos reales de InfluxDB; si no está disponible,
// cae automáticamente a datos mock para no romper la demo.

import type { GeoPoint } from '../types';
import { classifyVibration } from '../utils/vibration';

// ─── Config (valores en .env) ────────────────────────────────────
// En dev se usa el proxy de Vite (/influx). En producción, sobreescribe
// con VITE_INFLUX_URL=http://<servidor>:8086
const INFLUX_BASE   = (import.meta.env.VITE_INFLUX_URL as string | undefined) ?? '/influx';
const INFLUX_TOKEN  = (import.meta.env.VITE_INFLUX_TOKEN  as string | undefined) ?? 'my-super-secret-auth-token';
const INFLUX_ORG    = (import.meta.env.VITE_INFLUX_ORG    as string | undefined) ?? 'train_org';
const INFLUX_BUCKET = (import.meta.env.VITE_INFLUX_BUCKET as string | undefined) ?? 'vibrations';

// ─── Fuente de datos activa ───────────────────────────────────────
export type DataSource = 'live' | 'mock';
let _lastSource: DataSource = 'mock';
export function getLastSource(): DataSource { return _lastSource; }

// ─── Mock data ────────────────────────────────────────────────────
const MOCK_DATA: Omit<GeoPoint, 'level'>[] = [
  // — Tramo Jerez de la Frontera —
  { id: 'p001', lat: 36.6850, lon: -6.1261, vibration: 0.12, trainId: 'T-101', timestamp: '2026-03-05T08:15:00Z' },
  { id: 'p002', lat: 36.6870, lon: -6.1230, vibration: 0.25, trainId: 'T-101', timestamp: '2026-03-05T08:15:05Z' },
  { id: 'p003', lat: 36.6892, lon: -6.1198, vibration: 0.45, trainId: 'T-101', timestamp: '2026-03-05T08:15:10Z', anomalyType: 'vibración elevada' },
  { id: 'p004', lat: 36.6915, lon: -6.1170, vibration: 0.78, trainId: 'T-101', timestamp: '2026-03-05T08:15:15Z', anomalyType: 'pico crítico', frequencyHz: 42.5 },
  { id: 'p005', lat: 36.6938, lon: -6.1140, vibration: 0.82, trainId: 'T-101', timestamp: '2026-03-05T08:15:20Z', anomalyType: 'pico crítico', frequencyHz: 45.0 },
  // — Tramo hacia El Puerto de Santa María —
  { id: 'p006', lat: 36.6600, lon: -6.1400, vibration: 0.08, trainId: 'T-102', timestamp: '2026-03-05T09:00:00Z' },
  { id: 'p007', lat: 36.6550, lon: -6.1500, vibration: 0.15, trainId: 'T-102', timestamp: '2026-03-05T09:00:05Z' },
  { id: 'p008', lat: 36.6500, lon: -6.1600, vibration: 0.35, trainId: 'T-102', timestamp: '2026-03-05T09:00:10Z', anomalyType: 'vibración elevada' },
  { id: 'p009', lat: 36.6450, lon: -6.1700, vibration: 0.52, trainId: 'T-102', timestamp: '2026-03-05T09:00:15Z', anomalyType: 'vibración elevada', frequencyHz: 38.0 },
  { id: 'p010', lat: 36.6400, lon: -6.1800, vibration: 0.18, trainId: 'T-102', timestamp: '2026-03-05T09:00:20Z' },
  // — Tramo Cádiz —
  { id: 'p011', lat: 36.5270, lon: -6.2885, vibration: 0.10, trainId: 'T-103', timestamp: '2026-03-05T10:30:00Z' },
  { id: 'p012', lat: 36.5300, lon: -6.2850, vibration: 0.42, trainId: 'T-103', timestamp: '2026-03-05T10:30:05Z', anomalyType: 'vibración elevada' },
  { id: 'p013', lat: 36.5330, lon: -6.2810, vibration: 0.71, trainId: 'T-103', timestamp: '2026-03-05T10:30:10Z', anomalyType: 'pico crítico', frequencyHz: 50.2 },
  { id: 'p014', lat: 36.5360, lon: -6.2770, vibration: 0.20, trainId: 'T-103', timestamp: '2026-03-05T10:30:15Z' },
  { id: 'p015', lat: 36.5390, lon: -6.2730, vibration: 0.05, trainId: 'T-103', timestamp: '2026-03-05T10:30:20Z' },
  // — Sevilla → Jerez —
  { id: 'p101', lat: 37.3915, lon: -5.9755, vibration: 0.11, trainId: 'T-201', timestamp: '2026-03-05T09:00:00Z' },
  { id: 'p102', lat: 37.3450, lon: -5.9580, vibration: 0.18, trainId: 'T-201', timestamp: '2026-03-05T09:05:00Z' },
  { id: 'p103', lat: 37.1842, lon: -5.7830, vibration: 0.48, trainId: 'T-201', timestamp: '2026-03-05T09:15:00Z', anomalyType: 'vibración elevada' },
  { id: 'p104', lat: 36.9850, lon: -5.9320, vibration: 0.76, trainId: 'T-201', timestamp: '2026-03-05T09:25:00Z', anomalyType: 'pico crítico', frequencyHz: 44.2 },
  { id: 'p105', lat: 36.9210, lon: -6.0780, vibration: 0.85, trainId: 'T-201', timestamp: '2026-03-05T09:35:00Z', anomalyType: 'pico crítico', frequencyHz: 48.5 },
  { id: 'p106', lat: 36.8000, lon: -6.1150, vibration: 0.22, trainId: 'T-201', timestamp: '2026-03-05T09:45:00Z' },
  { id: 'p107', lat: 36.6852, lon: -6.1265, vibration: 0.09, trainId: 'T-201', timestamp: '2026-03-05T09:55:00Z' },
];

// ─── Utilidades ───────────────────────────────────────────────────
function enrichPoints(raw: Omit<GeoPoint, 'level'>[]): GeoPoint[] {
  return raw.map((p) => ({ ...p, level: classifyVibration(p.vibration) }));
}

// ─── Parser de Annotated CSV de InfluxDB ──────────────────────────
// InfluxDB v2 devuelve CSV anotado al usar la Flux API.
// Las líneas con # son metadatos; las líneas vacías separan tablas.
// Con pivot(), cada fila agrupa todos los campos de un mismo _time.
function parseInfluxCSV(csv: string): Omit<GeoPoint, 'level'>[] {
  const result: Omit<GeoPoint, 'level'>[] = [];
  let headers: string[] = [];

  for (const rawLine of csv.split('\n')) {
    const line = rawLine.trim();

    if (line.startsWith('#')) continue;
    if (line === '') { headers = []; continue; }   // nueva tabla → reset cabecera

    const cols = line.split(',');

    if (headers.length === 0) {
      headers = cols.map((h) => h.trim());
      continue;
    }

    const row: Record<string, string> = {};
    headers.forEach((h, i) => { if (h) row[h] = (cols[i] ?? '').trim(); });

    const lat    = parseFloat(row['lat']);
    const lon    = parseFloat(row['lon']);
    const accelZ = parseFloat(row['accel_z']);

    if (isNaN(lat) || isNaN(lon) || isNaN(accelZ)) continue;

    const time    = row['_time'] || new Date().toISOString();
    const trainId = row['trainId'] || 'T-unknown';

    // accel_z viene en m/s² desde el ICM-20948.
    // Restamos 1 g (9.81 m/s²) para obtener solo la vibración dinámica.
    const vibration = Math.abs(accelZ - 9.81) / 9.81;

    const point: Omit<GeoPoint, 'level'> = {
      id: `${time}-${trainId}`,
      lat,
      lon,
      vibration,
      trainId,
      timestamp: time,
    };

    // Frecuencia dominante (campo opcional que el ESP32 puede calcular y enviar)
    const freqHz = parseFloat(row['dominant_freq_hz']);
    if (!isNaN(freqHz)) point.frequencyHz = freqHz;

    // Tipo de anomalía derivado del nivel de vibración
    if (vibration >= 0.7)      point.anomalyType = 'pico crítico';
    else if (vibration >= 0.3) point.anomalyType = 'vibración elevada';

    result.push(point);
  }

  return result;
}

// ─── Fetch InfluxDB ───────────────────────────────────────────────
async function fetchFromInflux(): Promise<GeoPoint[]> {
  // Ordena DESCENDENTE y limita a 500 para que el punto más reciente
  // siempre quede incluido en el resultado (el bug original usaba desc:false,
  // devolviendo los 500 puntos MÁS ANTIGUOS → mostRecentTs = hace >2h → offline).
  const query = [
    `from(bucket: "${INFLUX_BUCKET}")`,
    `  |> range(start: -24h)`,
    `  |> filter(fn: (r) => r._measurement == "mqtt_consumer")`,
    `  |> pivot(rowKey:["_time"], columnKey: ["_field"], valueColumn: "_value")`,
    `  |> sort(columns: ["_time"], desc: true)`,
    `  |> limit(n: 500)`,
  ].join('\n');

  const res = await fetch(
    `${INFLUX_BASE}/api/v2/query?org=${encodeURIComponent(INFLUX_ORG)}`,
    {
      method: 'POST',
      headers: {
        'Authorization': `Token ${INFLUX_TOKEN}`,
        'Content-Type': 'application/vnd.flux',
        'Accept': 'application/csv',
      },
      body: query,
    },
  );

  if (!res.ok) {
    const body = await res.text();
    throw new Error(`InfluxDB ${res.status}: ${body.slice(0, 200)}`);
  }

  const csv = await res.text();
  const raw = parseInfluxCSV(csv);
  // No lanzar si está vacío: InfluxDB puede estar conectado pero sin datos recientes.
  // El caller marca dataSource='live' aunque sea vacío, para distinguir
  // "ESP32 no ha enviado datos aún" de "InfluxDB no disponible".
  return enrichPoints(raw);
}

// ─── API pública ──────────────────────────────────────────────────

/** Devuelve puntos GPS. Cae a mock solo si InfluxDB no está accesible. */
export async function fetchGeoPoints(): Promise<GeoPoint[]> {
  try {
    const points = await fetchFromInflux();
    _lastSource = 'live';   // InfluxDB responde → siempre 'live', incluso si vacío
    return points;
  } catch (err) {
    console.warn('[geoService] InfluxDB no disponible, usando datos mock:', err);
    _lastSource = 'mock';
    return enrichPoints(MOCK_DATA);
  }
}

/** Lista de IDs de tren únicos */
export async function fetchTrainIds(): Promise<string[]> {
  const points = await fetchGeoPoints();
  return [...new Set(points.map((p) => p.trainId))];
}

/** Lista de tipos de anomalía únicos */
export async function fetchAnomalyTypes(): Promise<string[]> {
  const points = await fetchGeoPoints();
  return [...new Set(points.filter((p) => p.anomalyType).map((p) => p.anomalyType!))];
}
