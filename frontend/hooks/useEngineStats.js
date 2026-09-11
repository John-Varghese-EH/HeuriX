import { useState, useCallback, useRef } from 'react';
import { useTauriEvent } from './useTauriEvent';
// Exponential moving average for smooth visualization
function ema(prev, current, alpha = 0.15) {
    return prev + alpha * (current - prev);
}
export function useEngineStats() {
    const [stats, setStats] = useState([]);
    const [latest, setLatest] = useState(null);
    const emaRef = useRef(null);
    useTauriEvent('heurix://stats', useCallback((payload) => {
        const raw = payload.data;
        // Apply EMA smoothing for professional feel
        const smoothed = emaRef.current
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
