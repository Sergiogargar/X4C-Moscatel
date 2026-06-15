// ─── Utilidades de color y clasificación de vibración ───
import type { VibrationLevel } from '../types';

/** Umbrales de vibración (en g) */
const THRESHOLDS = {
  warning: 0.3,
  critical: 0.7,
} as const;

/** Clasificar el nivel de vibración a partir del valor en g */
export function classifyVibration(vibration: number): VibrationLevel {
  if (vibration >= THRESHOLDS.critical) return 'critical';
  if (vibration >= THRESHOLDS.warning) return 'warning';
  return 'ok';
}

/** Mapa de nivel → color hex (para Leaflet y CSS) */
const LEVEL_COLORS: Record<VibrationLevel, string> = {
  ok:       '#10b981',
  warning:  '#f59e0b',
  critical: '#ef4444',
};

/** Obtener color hex a partir del valor de vibración */
export function getVibrationColor(vibration: number): string {
  return LEVEL_COLORS[classifyVibration(vibration)];
}

/** Obtener color hex a partir del nivel */
export function getLevelColor(level: VibrationLevel): string {
  return LEVEL_COLORS[level];
}

/** Radio del marcador proporcional al nivel de vibración */
export function getMarkerRadius(vibration: number): number {
  if (vibration >= THRESHOLDS.critical) return 12;
  if (vibration >= THRESHOLDS.warning) return 9;
  return 6;
}

/** Formatear valor de vibración para la UI */
export function formatVibration(vibration: number): string {
  return `${vibration.toFixed(3)} g`;
}

/** Formatear timestamp ISO a cadena legible */
export function formatTimestamp(iso: string): string {
  return new Date(iso).toLocaleString('es-ES', {
    day: '2-digit',
    month: '2-digit',
    year: 'numeric',
    hour: '2-digit',
    minute: '2-digit',
    second: '2-digit',
  });
}
