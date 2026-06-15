// ─── Tipos del dominio: datos GPS de vibración en vía férrea ───

/** Estado de severidad del punto de medición */
export type VibrationLevel = 'ok' | 'warning' | 'critical';

/** Punto GPS que envía el arquitecto con datos de vibración */
export interface GeoPoint {
  /** Identificador único del registro */
  id: string;
  /** Latitud (WGS-84) */
  lat: number;
  /** Longitud (WGS-84) */
  lon: number;
  /** Valor de vibración en g (aceleración gravitacional) */
  vibration: number;
  /** Nivel calculado a partir del umbral */
  level: VibrationLevel;
  /** Identificador del tren que generó la medición */
  trainId: string;
  /** Fecha/hora ISO de la medición */
  timestamp: string;
  /** Frecuencia dominante en Hz (análisis espectral) */
  frequencyHz?: number;
  /** Tipo de anomalía detectada, si la hay */
  anomalyType?: string;
}

/** Filtros que el usuario puede aplicar en el dashboard */
export interface DashboardFilters {
  /** Rango de fechas [inicio, fin] en ISO */
  dateRange: [string, string] | null;
  /** Filtrar por ID de tren concreto */
  trainId: string | null;
  /** Filtrar por tipo de anomalía */
  anomalyType: string | null;
}
