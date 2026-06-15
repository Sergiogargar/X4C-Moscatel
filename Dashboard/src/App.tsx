// src/App.tsx
import { useEffect, useState } from 'react';
import { GeoMap } from './components/GeoMap';
import { FilterBar } from './components/FilterBar';
import { useGeoData } from './hooks/useGeoData';
import { useDashboardStore } from './store/dashboardStore';

function useRelativeTime(date: Date | null): string {
  const [, setTick] = useState(0);
  useEffect(() => {
    const id = setInterval(() => setTick((t) => t + 1), 5000);
    return () => clearInterval(id);
  }, []);
  if (!date) return 'nunca';
  const secs = Math.floor((Date.now() - date.getTime()) / 1000);
  if (secs < 15)  return 'ahora mismo';
  if (secs < 60)  return `hace ${secs}s`;
  if (secs < 120) return 'hace 1 min';
  return `hace ${Math.floor(secs / 60)} min`;
}

export default function App() {
  const { points, loading, error, dataSource } = useGeoData();
  const { lastLiveUpdate, points: allPoints } = useDashboardStore();

  // Dispositivo online = tenemos datos en vivo Y el punto más reciente llegó en los últimos 2 min
  const mostRecentTs = allPoints.length > 0
    ? Math.max(...allPoints.map((p) => new Date(p.timestamp).getTime()))
    : 0;
  const deviceOnline = dataSource === 'live' && mostRecentTs > 0 &&
    (Date.now() - mostRecentTs) < 120_000;

  const lastUpdateText = useRelativeTime(lastLiveUpdate);

  return (
    <div className="flex h-screen flex-col bg-surface-900 text-gray-100 font-sans">
      {/* ─── Cabecera ─── */}
      <header className="flex items-center gap-4 border-b border-surface-600 px-6 py-3">
        <h1 className="font-mono text-lg font-bold tracking-wide text-white">
          Xerez<span className="text-blue-400">4</span>Change
        </h1>
        <span className="rounded bg-blue-500/20 px-2 py-0.5 font-mono text-xs text-blue-300">
          Dashboard v0.1
        </span>

        {/* Fuente de datos */}
        <span
          className={`flex items-center gap-1.5 rounded px-2 py-0.5 font-mono text-xs ${
            dataSource === 'live' ? 'bg-green-500/20 text-green-300' : 'bg-amber-500/20 text-amber-300'
          }`}
        >
          <span className={`inline-block h-1.5 w-1.5 rounded-full ${
            dataSource === 'live' ? 'animate-pulse bg-green-400' : 'bg-amber-400'
          }`} />
          {dataSource === 'live' ? 'En vivo · InfluxDB' : 'Simulado · Mock'}
        </span>

        {/* Estado del dispositivo ESP32 */}
        <span className={`flex items-center gap-1.5 rounded px-2 py-0.5 font-mono text-xs ${
          deviceOnline ? 'bg-green-500/20 text-green-300' : 'bg-red-500/20 text-red-300'
        }`}>
          <span className={`inline-block h-1.5 w-1.5 rounded-full ${
            deviceOnline ? 'animate-pulse bg-green-400' : 'bg-red-500'
          }`} />
          {deviceOnline ? 'ESP32 online' : 'ESP32 offline'}
        </span>

        {/* Última recepción */}
        {dataSource === 'live' && (
          <span className="font-mono text-xs text-gray-500">
            Último dato: <span className="text-gray-300">{lastUpdateText}</span>
          </span>
        )}

        <span className="ml-auto font-mono text-xs text-gray-500">
          {new Date().toLocaleDateString('es-ES', {
            weekday: 'long', year: 'numeric', month: 'long', day: 'numeric',
          })}
        </span>
      </header>

      {/* ─── Barra de filtros ─── */}
      <div className="px-6 pt-4">
        <FilterBar />
      </div>

      {/* ─── Panel principal: GeoMap ─── */}
      <main className="flex-1 p-6">
        {loading && (
          <div className="flex h-full items-center justify-center">
            <div className="h-8 w-8 animate-spin rounded-full border-2 border-blue-400 border-t-transparent" />
          </div>
        )}
        {error && (
          <div className="flex h-full items-center justify-center">
            <p className="font-mono text-sm text-red-400">Error: {error}</p>
          </div>
        )}
        {!loading && !error && <GeoMap data={points} />}
      </main>
    </div>
  );
}
