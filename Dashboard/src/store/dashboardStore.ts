// ─── Store global del dashboard (Zustand) ───
import { create } from 'zustand';
import type { DashboardFilters, GeoPoint } from '../types';

interface DashboardState {
  /** Todos los puntos cargados */
  points: GeoPoint[];
  /** Puntos filtrados para la vista actual */
  filteredPoints: GeoPoint[];
  /** Filtros activos */
  filters: DashboardFilters;
  /** Estado de carga */
  loading: boolean;
  /** Error, si lo hay */
  error: string | null;
  /** Timestamp de la última actualización con datos reales de InfluxDB */
  lastLiveUpdate: Date | null;

  // ─ Acciones ─
  setPoints: (points: GeoPoint[]) => void;
  setFilters: (filters: Partial<DashboardFilters>) => void;
  resetFilters: () => void;
  setLoading: (loading: boolean) => void;
  setError: (error: string | null) => void;
  setLastLiveUpdate: (date: Date) => void;
}

const DEFAULT_FILTERS: DashboardFilters = {
  dateRange: null,
  trainId: null,
  anomalyType: null,
};

/** Aplicar filtros sobre los puntos */
function applyFilters(points: GeoPoint[], filters: DashboardFilters): GeoPoint[] {
  return points.filter((p) => {
    // Filtro por rango de fechas
    if (filters.dateRange) {
      const ts = new Date(p.timestamp).getTime();
      const [start, end] = filters.dateRange.map((d) => new Date(d).getTime());
      if (ts < start || ts > end) return false;
    }
    // Filtro por ID de tren
    if (filters.trainId && p.trainId !== filters.trainId) return false;
    // Filtro por tipo de anomalía
    if (filters.anomalyType && p.anomalyType !== filters.anomalyType) return false;
    return true;
  });
}

export const useDashboardStore = create<DashboardState>((set, get) => ({
  points: [],
  filteredPoints: [],
  filters: { ...DEFAULT_FILTERS },
  loading: false,
  error: null,
  lastLiveUpdate: null,

  setPoints: (points) => {
    const filtered = applyFilters(points, get().filters);
    set({ points, filteredPoints: filtered, loading: false });
  },

  setFilters: (partial) => {
    const filters = { ...get().filters, ...partial };
    const filtered = applyFilters(get().points, filters);
    set({ filters, filteredPoints: filtered });
  },

  resetFilters: () => {
    set({ filters: { ...DEFAULT_FILTERS }, filteredPoints: get().points });
  },

  setLoading: (loading) => set({ loading }),
  setError: (error) => set({ error, loading: false }),
  setLastLiveUpdate: (date) => set({ lastLiveUpdate: date }),
}));
