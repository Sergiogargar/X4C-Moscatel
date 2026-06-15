// src/components/GeoMap.tsx
import { MapContainer, TileLayer, CircleMarker, Popup, Tooltip } from 'react-leaflet';
import 'leaflet/dist/leaflet.css';
import type { GeoPoint } from '../types';
import {
  getVibrationColor,
  getMarkerRadius,
  formatVibration,
  formatTimestamp,
  getLevelColor,
} from '../utils/vibration';

// ─── Leyenda de colores ─────────────────────────────────────────
const LEGEND_ITEMS: { label: string; level: 'ok' | 'warning' | 'critical'; range: string }[] = [
  { label: 'Normal',   level: 'ok',       range: '< 0.3 g' },
  { label: 'Aviso',    level: 'warning',  range: '0.3 – 0.7 g' },
  { label: 'Crítico',  level: 'critical', range: '> 0.7 g' },
];

function Legend() {
  return (
    <div className="absolute bottom-4 left-4 z-[1000] rounded-lg bg-surface-800/90 px-4 py-3 font-mono text-xs backdrop-blur">
      <p className="mb-2 font-semibold text-gray-300 uppercase tracking-wider">Vibración</p>
      {LEGEND_ITEMS.map((item) => (
        <div key={item.level} className="flex items-center gap-2 py-0.5">
          <span
            className="inline-block h-3 w-3 rounded-full"
            style={{ backgroundColor: getLevelColor(item.level) }}
          />
          <span className="text-gray-400">{item.label}</span>
          <span className="ml-auto text-gray-500">{item.range}</span>
        </div>
      ))}
    </div>
  );
}

// ─── Resumen rápido ─────────────────────────────────────────────
function Stats({ points }: { points: GeoPoint[] }) {
  const total = points.length;
  const critical = points.filter((p) => p.level === 'critical').length;
  const warning  = points.filter((p) => p.level === 'warning').length;

  return (
    <div className="absolute top-4 right-4 z-[1000] flex gap-3 font-mono text-xs">
      <span className="rounded bg-surface-800/90 px-3 py-1.5 backdrop-blur">
        Puntos: <strong className="text-white">{total}</strong>
      </span>
      <span className="rounded bg-surface-800/90 px-3 py-1.5 backdrop-blur">
        Avisos: <strong className="text-amber-400">{warning}</strong>
      </span>
      <span className="rounded bg-surface-800/90 px-3 py-1.5 backdrop-blur">
        Críticos: <strong className="text-red-400">{critical}</strong>
      </span>
    </div>
  );
}

// ─── Componente principal ───────────────────────────────────────
interface GeoMapProps {
  data: GeoPoint[];
}

/** Centro de la zona Jerez–Cádiz */
const DEFAULT_CENTER: [number, number] = [36.62, -6.20];
const DEFAULT_ZOOM = 12;

export function GeoMap({ data }: GeoMapProps) {
  return (
    <div className="relative h-full w-full overflow-hidden rounded-xl border border-surface-600">
      <MapContainer
        center={DEFAULT_CENTER}
        zoom={DEFAULT_ZOOM}
        className="h-full w-full"
        zoomControl={false}
      >
        {/* Capa CartoDB Dark Matter — modo oscuro */}
        <TileLayer
          url="https://{s}.basemaps.cartocdn.com/dark_all/{z}/{x}/{y}{r}.png"
          attribution='&copy; <a href="https://www.openstreetmap.org/copyright">OSM</a> &copy; <a href="https://carto.com/">CARTO</a>'
        />

        {data.map((point) => {
          const color = getVibrationColor(point.vibration);
          return (
            <CircleMarker
              key={point.id}
              center={[point.lat, point.lon]}
              pathOptions={{
                color,
                fillColor: color,
                fillOpacity: 0.85,
                weight: point.level === 'critical' ? 2 : 1,
              }}
              radius={getMarkerRadius(point.vibration)}
            >
              <Tooltip
                direction="top"
                offset={[0, -8]}
                className="!bg-surface-700 !border-surface-600 !text-gray-200 !font-mono !text-xs !rounded-lg !px-3 !py-2"
              >
                <span className="font-semibold">{point.trainId}</span> · {formatVibration(point.vibration)}
              </Tooltip>

              <Popup className="geo-popup">
                <div className="font-mono text-xs leading-relaxed">
                  <p className="mb-1 text-sm font-semibold" style={{ color }}>
                    {point.level === 'critical' ? '⚠ CRÍTICO' : point.level === 'warning' ? '⚡ Aviso' : '✓ Normal'}
                  </p>
                  <p><span className="text-gray-400">Tren:</span> {point.trainId}</p>
                  <p><span className="text-gray-400">Vibración:</span> {formatVibration(point.vibration)}</p>
                  {point.frequencyHz && (
                    <p><span className="text-gray-400">Frecuencia:</span> {point.frequencyHz} Hz</p>
                  )}
                  {point.anomalyType && (
                    <p><span className="text-gray-400">Anomalía:</span> {point.anomalyType}</p>
                  )}
                  <p className="mt-1 text-gray-500">{formatTimestamp(point.timestamp)}</p>
                </div>
              </Popup>
            </CircleMarker>
          );
        })}
      </MapContainer>

      {/* Overlays */}
      <Legend />
      <Stats points={data} />
    </div>
  );
}