// src/components/FilterBar.tsx
import { useMemo } from 'react';
import { useDashboardStore } from '../store/dashboardStore';

export function FilterBar() {
  const { filters, setFilters, resetFilters, points } = useDashboardStore();

  // Derivar opciones de los datos ya cargados en el store (sin queries extra a InfluxDB)
  const trainIds = useMemo(
    () => [...new Set(points.map((p) => p.trainId))].sort(),
    [points],
  );
  const anomalyTypes = useMemo(
    () => [...new Set(points.filter((p) => p.anomalyType).map((p) => p.anomalyType!))].sort(),
    [points],
  );

  return (
    <div className="flex flex-wrap items-center gap-3 rounded-lg bg-surface-800 px-4 py-3 font-mono text-xs">
      <span className="text-gray-400 uppercase tracking-wider font-semibold">Filtros</span>

      {/* Fecha inicio */}
      <input
        type="date"
        className="rounded bg-surface-700 px-2 py-1 text-gray-200 outline-none focus:ring-1 focus:ring-blue-500"
        value={filters.dateRange?.[0]?.slice(0, 10) ?? ''}
        onChange={(e) => {
          const start = e.target.value ? e.target.value + 'T00:00:00Z' : null;
          const end = filters.dateRange?.[1] ?? null;
          setFilters({ dateRange: start && end ? [start, end] : start ? [start, '2099-12-31T23:59:59Z'] : null });
        }}
      />
      <span className="text-gray-500">→</span>
      {/* Fecha fin */}
      <input
        type="date"
        className="rounded bg-surface-700 px-2 py-1 text-gray-200 outline-none focus:ring-1 focus:ring-blue-500"
        value={filters.dateRange?.[1]?.slice(0, 10) ?? ''}
        onChange={(e) => {
          const start = filters.dateRange?.[0] ?? null;
          const end = e.target.value ? e.target.value + 'T23:59:59Z' : null;
          setFilters({ dateRange: start && end ? [start, end] : null });
        }}
      />

      {/* Selector de tren */}
      <select
        className="rounded bg-surface-700 px-2 py-1 text-gray-200 outline-none focus:ring-1 focus:ring-blue-500"
        value={filters.trainId ?? ''}
        onChange={(e) => setFilters({ trainId: e.target.value || null })}
      >
        <option value="">Todos los trenes</option>
        {trainIds.map((id) => (
          <option key={id} value={id}>{id}</option>
        ))}
      </select>

      {/* Selector de anomalía */}
      <select
        className="rounded bg-surface-700 px-2 py-1 text-gray-200 outline-none focus:ring-1 focus:ring-blue-500"
        value={filters.anomalyType ?? ''}
        onChange={(e) => setFilters({ anomalyType: e.target.value || null })}
      >
        <option value="">Todas las anomalías</option>
        {anomalyTypes.map((t) => (
          <option key={t} value={t}>{t}</option>
        ))}
      </select>

      {/* Reset */}
      <button
        onClick={resetFilters}
        className="ml-auto rounded bg-surface-600 px-3 py-1 text-gray-300 transition hover:bg-surface-700 hover:text-white"
      >
        Limpiar
      </button>
    </div>
  );
}
