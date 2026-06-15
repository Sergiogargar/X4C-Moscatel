// ─── Hook para cargar y gestionar datos GPS ───────────────────────
import { useEffect, useRef, useState } from 'react';
import { fetchGeoPoints, getLastSource, type DataSource } from '../services/geoService';
import { useDashboardStore } from '../store/dashboardStore';

// Intervalo de polling configurable por .env (defecto 10 s)
const POLL_MS = parseInt(
  (import.meta.env.VITE_POLL_INTERVAL_MS as string | undefined) ?? '10000',
  10,
);

export function useGeoData() {
  const { setPoints, setLoading, setError, setLastLiveUpdate, filteredPoints, loading, error } =
    useDashboardStore();

  const [dataSource, setDataSource] = useState<DataSource>('mock');
  const isFirstLoad = useRef(true);

  useEffect(() => {
    let cancelled = false;

    async function load() {
      // Solo mostrar spinner en la carga inicial; en polls silenciosos
      if (isFirstLoad.current) setLoading(true);

      try {
        const data = await fetchGeoPoints();
        if (!cancelled) {
          setPoints(data);
          const source = getLastSource();
          setDataSource(source);
          if (source === 'live') setLastLiveUpdate(new Date());
          isFirstLoad.current = false;
        }
      } catch (err) {
        if (!cancelled) {
          setError(err instanceof Error ? err.message : 'Error desconocido');
          isFirstLoad.current = false;
        }
      }
    }

    load();
    const timer = setInterval(load, POLL_MS);

    return () => {
      cancelled = true;
      clearInterval(timer);
    };
  }, [setPoints, setLoading, setError, setLastLiveUpdate]);

  return { points: filteredPoints, loading, error, dataSource };
}
