import { useState, useCallback, useRef } from 'react';
import { useTauriEvent } from './useTauriEvent';

export interface SystemStats {
  cpu_percent: number;
  mem_percent: number;
  mem_used_mb: number;
  mem_total_mb: number;
  io_read_mb: number;
  io_write_mb: number;
}

// Exponential moving average for smooth visualization
function ema(prev: number, current: number, alpha = 0.15): number {
  return prev + alpha * (current - prev);
}

export function useEngineStats() {
  const [stats, setStats] = useState<SystemStats[]>([]);
  const [latest, setLatest] = useState<SystemStats | null>(null);
  const emaRef = useRef<SystemStats | null>(null);

  useTauriEvent<{type: string, data: SystemStats}>('heurix://stats', useCallback((payload) => {
    const raw = payload.data;

    // Apply EMA smoothing for professional feel
    const smoothed: SystemStats = emaRef.current
      ? {
          cpu_percent: ema(emaRef.current.cpu_percent, raw.cpu_percent),
          mem_percent: ema(emaRef.current.mem_percent, raw.mem_percent),
          mem_used_mb: raw.mem_used_mb, // Don't smooth absolute values
          mem_total_mb: raw.mem_total_mb,
          io_read_mb: ema(emaRef.current.io_read_mb, raw.io_read_mb, 0.25),
          io_write_mb: ema(emaRef.current.io_write_mb, raw.io_write_mb, 0.25),
        }
      : raw;

    emaRef.current = smoothed;
    setLatest(smoothed);
    setStats((prev) => {
      const next = [...prev, smoothed];
      if (next.length > 600) {
        return next.slice(next.length - 600);
      }
      return next;
    });
  }, []));

  return { stats, latest };
}