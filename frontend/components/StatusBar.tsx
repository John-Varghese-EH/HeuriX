

interface StatusBarProps {
  status: string;
  eventRate: number;
  uptime: string;
}

export function StatusBar({ status, eventRate, uptime }: StatusBarProps) {
  const isRunning = status === 'running';
  const isError = status === 'error' || status === 'critical';

  return (
    <div className="hx-statusbar">
      <div style={{ display: 'flex', alignItems: 'center', gap: '8px' }}>
        <div className={`hx-status-dot ${isRunning ? 'hx-status-dot--running' : isError ? 'hx-status-dot--error' : ''}`} />
        <span style={{ fontWeight: 500, color: isRunning ? 'var(--hx-text-primary)' : 'var(--hx-text-secondary)' }}>
          {status.charAt(0).toUpperCase() + status.slice(1)}
        </span>
      </div>
      <div>
        <span style={{ color: 'var(--hx-text-secondary)' }}>{eventRate} events/sec</span>
      </div>
      <div style={{ display: 'flex', gap: '16px' }}>
        <span>Uptime: {uptime}</span>
        <span style={{ color: 'var(--hx-border-focus)' }}>|</span>
        <span style={{ color: 'var(--hx-text-dim)' }}>v0.1.0</span>
      </div>
    </div>
  );
}
