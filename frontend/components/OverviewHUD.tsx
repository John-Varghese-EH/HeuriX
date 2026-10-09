import { Shield, ShieldAlert, Activity, Terminal, AlertTriangle, CheckCircle, ShieldCheck, FolderLock } from 'lucide-react';
import { PieChart, Pie, Cell, ResponsiveContainer } from 'recharts';
import { FeedItem } from './EventFeed';

export interface Alert {
  severity: 'info' | 'low' | 'medium' | 'high' | 'critical';
  description: string;
  entropy?: number;
  pid: number;
  process_name: string;
  action: string;
  timestamp_ms: number;
  quarantine_path?: string | null;
  killed_pids?: number[] | null;
  threat_score?: number | null;
}

interface OverviewHUDProps {
  alerts: Alert[];
  threatsMitigated: number;
  targetPath: string;
  eventRate: number;
  uptime: string;
  events: FeedItem[];
  engineStatus: string;
}

export function OverviewHUD({ alerts, threatsMitigated, targetPath, eventRate, uptime, events, engineStatus }: OverviewHUDProps) {
  const recentAlerts = alerts.slice(-5).reverse();
  const criticalCount = alerts.filter(a => a.severity === 'critical' && (Date.now() - a.timestamp_ms) < 60000).length;
  const status = criticalCount > 0 ? 'critical' : (alerts.length > 0 ? 'warning' : 'secure');

  // Real Threat Level Calculation (Dynamic based on live Event Rate + Alerts)
  const rateThreat = Math.min(20, Math.floor(eventRate / 2.5));
  const alertThreat = criticalCount > 0 ? 80 : (alerts.length > 0 ? 30 : 0);
  const totalThreat = Math.min(100, Math.max(0, rateThreat + alertThreat));

  const threatLevelData = [
    { name: 'Threat Level', value: totalThreat },
    { name: 'Safe', value: 100 - totalThreat }
  ];

  const COLORS: Record<string, string[]> = {
    secure: ['var(--hx-accent-green)', 'var(--hx-border)'],
    warning: ['var(--hx-accent-amber)', 'var(--hx-border)'],
    critical: ['var(--hx-accent-red)', 'var(--hx-border)']
  };

  const GAUGE_GLOW: Record<string, string> = {
    secure: 'rgba(16, 185, 129, 0.25)',
    warning: 'rgba(245, 158, 11, 0.25)',
    critical: 'rgba(239, 68, 68, 0.25)'
  };

  // Live System Integrity Matrix powered by real filesystem events
  const fsEvents = events.filter(e => e.type === 'fs').map(e => e.data as any);
  const recentPaths = fsEvents.slice(-16).reverse(); // Newest first
  
  const getHexHash = (str: string) => {
    let hash = 0;
    for (let i = 0; i < str.length; i++) {
      hash = (hash << 5) - hash + str.charCodeAt(i);
      hash |= 0;
    }
    return '0x' + Math.abs(hash).toString(16).padStart(6, '0').substring(0, 6).toUpperCase();
  };

  const matrixBlocks = Array.from({ length: 16 }).map((_, i) => {
    const ev = recentPaths[i];
    return ev ? getHexHash(ev.path) : '--------';
  });

  const isEngineOnline = engineStatus === 'running';

  return (
    <div className="hx-page-container">
      <div className="hx-header-row" style={{ marginBottom: '32px' }}>
        <div>
          <h1 className="hx-page-title" style={{ display: 'flex', alignItems: 'center', gap: '12px' }}>
            Dashboard
            {status === 'secure' && <div style={{ fontSize: '12px', padding: '4px 12px', background: 'var(--hx-overlay-5)', color: 'var(--hx-accent-green)', borderRadius: '20px', border: '1px solid var(--hx-border)', display: 'flex', alignItems: 'center', gap: '6px' }}><CheckCircle size={14}/> SECURE</div>}
            {status === 'warning' && <div style={{ fontSize: '12px', padding: '4px 12px', background: 'var(--hx-overlay-5)', color: 'var(--hx-accent-amber)', borderRadius: '20px', border: '1px solid var(--hx-border)', display: 'flex', alignItems: 'center', gap: '6px' }}><AlertTriangle size={14}/> ELEVATED RISK</div>}
            {status === 'critical' && <div style={{ fontSize: '12px', padding: '4px 12px', background: 'var(--hx-overlay-5)', color: 'var(--hx-accent-red)', borderRadius: '20px', border: '1px solid var(--hx-accent-red)', display: 'flex', alignItems: 'center', gap: '6px', animation: 'soft-pulse 2s infinite' }}><ShieldAlert size={14}/> THREAT DETECTED</div>}
          </h1>
          <p className="hx-page-desc">High-level threat intelligence and active mitigation status.</p>
        </div>
      </div>

      {/* Top Section: Threat Gauge + Stat Cards */}
      <div style={{ display: 'grid', gridTemplateColumns: '1fr 2.5fr', gap: '24px', marginBottom: '24px' }}>
        {/* Threat Level Gauge */}
        <div className="panel" style={{ padding: '24px', display: 'flex', flexDirection: 'column', alignItems: 'center', justifyContent: 'center', position: 'relative', overflow: 'hidden' }}>
          <div style={{ position: 'absolute', top: 0, left: 0, right: 0, height: '100%', background: `radial-gradient(circle at 50% 120%, ${GAUGE_GLOW[status]}, transparent 60%)`, pointerEvents: 'none' }} />
          <h3 style={{ fontSize: '14px', color: 'var(--hx-text-secondary)', position: 'absolute', top: '24px', left: '24px', textTransform: 'uppercase', letterSpacing: '0.05em' }}>Risk Assessment</h3>
          <div style={{ width: '100%', height: '200px', marginTop: '20px', position: 'relative', zIndex: 1 }}>
            <ResponsiveContainer width="100%" height="100%">
              <PieChart>
                <Pie
                  data={threatLevelData}
                  cx="50%"
                  cy="70%"
                  startAngle={180}
                  endAngle={0}
                  innerRadius={70}
                  outerRadius={90}
                  paddingAngle={2}
                  dataKey="value"
                  stroke="none"
                >
                  {threatLevelData.map((_, index) => (
                    <Cell key={`cell-${index}`} fill={COLORS[status][index]} style={{ filter: index === 0 ? `drop-shadow(0 0 16px ${COLORS[status][index]})` : 'none', transition: 'all 0.5s cubic-bezier(0.16, 1, 0.3, 1)' }} />
                  ))}
                </Pie>
              </PieChart>
            </ResponsiveContainer>
            <div style={{ position: 'absolute', top: '55%', left: '50%', transform: 'translate(-50%, -50%)', textAlign: 'center' }}>
              <div style={{ fontSize: '36px', fontWeight: 700, color: COLORS[status][0], fontFamily: 'var(--hx-font-mono)', textShadow: `0 0 20px ${GAUGE_GLOW[status]}` }}>
                {threatLevelData[0].value}%
              </div>
            </div>
          </div>
        </div>

        {/* Highlight Stats */}
        <div style={{ display: 'grid', gridTemplateColumns: 'repeat(2, 1fr)', gap: '24px' }}>
          <div className="panel hx-stat-card" style={{ position: 'relative', overflow: 'hidden' }}>
            <div style={{ position: 'absolute', top: 0, right: 0, width: '150px', height: '150px', background: 'radial-gradient(circle, rgba(59, 130, 246, 0.1) 0%, transparent 70%)', transform: 'translate(30%, -30%)' }} />
            <div className="hx-stat-card__header">
              <span className="hx-stat-card__label">Threats Mitigated</span>
              <Shield size={18} color="var(--hx-accent-blue)" />
            </div>
            <div className="hx-stat-card__value" style={{ textShadow: '0 0 20px rgba(59, 130, 246, 0.4)', fontSize: '32px' }}>{threatsMitigated}</div>
            <div style={{ position: 'absolute', bottom: 0, left: 0, right: 0, height: '3px', background: 'linear-gradient(90deg, var(--hx-accent-blue), transparent)' }} />
          </div>

          <div className="panel hx-stat-card" style={{ position: 'relative', overflow: 'hidden' }}>
            <div style={{ position: 'absolute', top: 0, right: 0, width: '150px', height: '150px', background: 'radial-gradient(circle, rgba(245, 158, 11, 0.1) 0%, transparent 70%)', transform: 'translate(30%, -30%)' }} />
            <div className="hx-stat-card__header">
              <span className="hx-stat-card__label">Filesystem Event Rate</span>
              <Activity size={18} color="var(--hx-accent-amber)" />
            </div>
            <div className="hx-stat-card__value" style={{ textShadow: '0 0 20px rgba(245, 158, 11, 0.4)', fontSize: '32px' }}>{eventRate}<span style={{fontSize:'16px', color:'var(--hx-text-dim)'}}>/s</span></div>
            <div style={{ position: 'absolute', bottom: 0, left: 0, right: 0, height: '3px', background: 'linear-gradient(90deg, var(--hx-accent-amber), transparent)' }} />
          </div>

          <div className="panel hx-stat-card" style={{ position: 'relative', overflow: 'hidden' }}>
            <div style={{ position: 'absolute', top: 0, right: 0, width: '150px', height: '150px', background: 'radial-gradient(circle, rgba(16, 185, 129, 0.1) 0%, transparent 70%)', transform: 'translate(30%, -30%)' }} />
            <div className="hx-stat-card__header">
              <span className="hx-stat-card__label">Active Target</span>
              <FolderLock size={18} color="var(--hx-accent-green)" />
            </div>
            <div className="hx-stat-card__value" style={{ textShadow: '0 0 20px rgba(16, 185, 129, 0.4)', fontSize: '18px', whiteSpace: 'nowrap', overflow: 'hidden', textOverflow: 'ellipsis' }}>{targetPath}</div>
            <div style={{ position: 'absolute', bottom: 0, left: 0, right: 0, height: '3px', background: 'linear-gradient(90deg, var(--hx-accent-green), transparent)' }} />
          </div>
          
          <div className="panel hx-stat-card" style={{ position: 'relative', overflow: 'hidden' }}>
            <div style={{ position: 'absolute', top: 0, right: 0, width: '150px', height: '150px', background: 'radial-gradient(circle, rgba(168, 85, 247, 0.1) 0%, transparent 70%)', transform: 'translate(30%, -30%)' }} />
            <div className="hx-stat-card__header">
              <span className="hx-stat-card__label">Engine Uptime</span>
              <Terminal size={18} color="var(--hx-accent-purple)" />
            </div>
            <div className="hx-stat-card__value" style={{ textShadow: '0 0 20px rgba(168, 85, 247, 0.4)', fontSize: '32px' }}>{uptime}</div>
            <div style={{ position: 'absolute', bottom: 0, left: 0, right: 0, height: '3px', background: 'linear-gradient(90deg, var(--hx-accent-purple), transparent)' }} />
          </div>
        </div>
      </div>

      {/* Middle Section: Integrity Matrix + Watchdogs */}
      <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: '24px', marginBottom: '24px' }}>
        <div className="panel" style={{ padding: '24px', position: 'relative', overflow: 'hidden' }}>
          <div style={{ position: 'absolute', bottom: 0, left: 0, width: '100%', height: '50%', background: `linear-gradient(0deg, ${status === 'critical' ? 'rgba(239, 68, 68, 0.05)' : 'rgba(16, 185, 129, 0.05)'} 0%, transparent 100%)`, pointerEvents: 'none' }} />
          <h2 style={{ fontSize: '15px', fontWeight: 600, marginBottom: '16px', display: 'flex', alignItems: 'center', justifyContent: 'space-between' }}>
            Live I/O Activity
            <span style={{ fontSize: '11px', color: status === 'critical' ? 'var(--hx-accent-red)' : 'var(--hx-accent-green)', letterSpacing: '0.1em', animation: status === 'critical' ? 'soft-pulse 1s infinite' : 'none' }}>
              {status === 'critical' ? 'THREAT DETECTED' : 'SECURE'}
            </span>
          </h2>
          <div style={{ display: 'grid', gridTemplateColumns: 'repeat(4, 1fr)', gap: '8px', position: 'relative', zIndex: 1 }}>
            {matrixBlocks.map((hex, i) => (
              <div key={i} style={{ 
                height: '32px', 
                background: hex !== '--------' ? (status === 'critical' && i % 3 === 0 ? 'rgba(239, 68, 68, 0.2)' : 'rgba(16, 185, 129, 0.15)') : 'rgba(16, 185, 129, 0.02)',
                border: `1px solid ${hex !== '--------' ? (status === 'critical' && i % 3 === 0 ? 'var(--hx-accent-red)' : 'var(--hx-accent-green)') : 'var(--hx-border)'}`,
                borderRadius: '4px',
                display: 'flex', alignItems: 'center', justifyContent: 'center',
                color: hex !== '--------' ? (status === 'critical' && i % 3 === 0 ? 'var(--hx-accent-red)' : 'var(--hx-accent-green)') : 'var(--hx-text-dim)',
                fontSize: '11px', fontFamily: 'var(--hx-font-mono)', fontWeight: hex !== '--------' ? 600 : 400,
                boxShadow: status === 'critical' && i % 3 === 0 ? '0 0 10px rgba(239, 68, 68, 0.4)' : (hex !== '--------' ? '0 0 8px rgba(16, 185, 129, 0.2)' : 'none'),
                animation: status === 'critical' && i % 3 === 0 ? 'soft-pulse 1s infinite' : 'none',
                transition: 'all 0.3s ease',
                overflow: 'hidden',
                textOverflow: 'ellipsis',
                whiteSpace: 'nowrap',
                padding: '0 4px'
              }}>
                {hex}
              </div>
            ))}
          </div>
        </div>

        <div className="panel" style={{ padding: '24px' }}>
          <h2 style={{ fontSize: '15px', fontWeight: 600, marginBottom: '16px' }}>Active Watchdogs</h2>
          <div style={{ display: 'flex', flexDirection: 'column', gap: '12px' }}>
            <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', padding: '16px', background: 'var(--hx-overlay-2)', borderRadius: '8px', border: '1px solid var(--hx-border)', transition: 'all 0.2s', cursor: 'default' }}>
              <div style={{ display: 'flex', alignItems: 'center', gap: '16px' }}>
                <div style={{ padding: '8px', background: 'rgba(59, 130, 246, 0.1)', borderRadius: '6px' }}>
                  <Activity size={18} color="var(--hx-accent-blue)" />
                </div>
                <div>
                  <div style={{ fontSize: '14px', fontWeight: 600, color: 'var(--hx-text-primary)' }}>Heuristic Analysis Engine</div>
                  <div style={{ fontSize: '12px', color: 'var(--hx-text-secondary)', marginTop: '2px' }}>Entropy & Burst Detection</div>
                </div>
              </div>
              {isEngineOnline ? (
                <div style={{ fontSize: '12px', color: 'var(--hx-accent-green)', display: 'flex', alignItems: 'center', gap: '6px', fontWeight: 600 }}><div style={{width: 8, height: 8, borderRadius: '50%', background: 'var(--hx-accent-green)', boxShadow: '0 0 8px var(--hx-accent-green)'}}/> ONLINE</div>
              ) : (
                <div style={{ fontSize: '12px', color: 'var(--hx-text-dim)', display: 'flex', alignItems: 'center', gap: '6px', fontWeight: 600 }}><div style={{width: 8, height: 8, borderRadius: '50%', background: 'var(--hx-text-dim)'}}/> OFFLINE</div>
              )}
            </div>
            
            <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', padding: '16px', background: 'var(--hx-overlay-2)', borderRadius: '8px', border: '1px solid var(--hx-border)', transition: 'all 0.2s', cursor: 'default' }}>
              <div style={{ display: 'flex', alignItems: 'center', gap: '16px' }}>
                <div style={{ padding: '8px', background: 'rgba(168, 85, 247, 0.1)', borderRadius: '6px' }}>
                  <Terminal size={18} color="var(--hx-accent-purple)" />
                </div>
                <div>
                  <div style={{ fontSize: '14px', fontWeight: 600, color: 'var(--hx-text-primary)' }}>Process Mitigation Hook</div>
                  <div style={{ fontSize: '12px', color: 'var(--hx-text-secondary)', marginTop: '2px' }}>Process State Control</div>
                </div>
              </div>
              {isEngineOnline ? (
                <div style={{ fontSize: '12px', color: 'var(--hx-accent-green)', display: 'flex', alignItems: 'center', gap: '6px', fontWeight: 600 }}><div style={{width: 8, height: 8, borderRadius: '50%', background: 'var(--hx-accent-green)', boxShadow: '0 0 8px var(--hx-accent-green)'}}/> ENABLED</div>
              ) : (
                <div style={{ fontSize: '12px', color: 'var(--hx-text-dim)', display: 'flex', alignItems: 'center', gap: '6px', fontWeight: 600 }}><div style={{width: 8, height: 8, borderRadius: '50%', background: 'var(--hx-text-dim)'}}/> DISABLED</div>
              )}
            </div>
          </div>
        </div>
      </div>

      {/* Bottom Section: Threat Log */}
      <div>
        <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: '16px' }}>
          <h2 style={{ fontSize: '15px', fontWeight: 600 }}>Active Threat Log</h2>
          <span style={{ fontSize: '12px', color: 'var(--hx-text-secondary)' }}>Showing last 5 security events</span>
        </div>
        
        {recentAlerts.length === 0 ? (
          <div className="panel" style={{ padding: '64px', textAlign: 'center', display: 'flex', flexDirection: 'column', alignItems: 'center', gap: '16px', border: '1px dashed var(--hx-border)' }}>
            <ShieldCheck size={48} color="var(--hx-accent-green)" style={{ opacity: 0.2 }} />
            <div style={{ color: 'var(--hx-text-secondary)', fontSize: '14px', fontWeight: 500 }}>No recent alerts. System is fully secure.</div>
          </div>
        ) : (
          <div className="hx-table-container" style={{ border: '1px solid var(--hx-border)', boxShadow: 'var(--hx-shadow-lg)' }}>
            <table className="hx-table">
              <thead>
                <tr style={{ background: 'var(--hx-overlay-2)' }}>
                  <th>Severity</th>
                  <th>Target Process</th>
                  <th>Action Taken</th>
                  <th>Time</th>
                </tr>
              </thead>
              <tbody>
                {recentAlerts.map((alert, i) => {
                  const safeTime = alert.timestamp_ms ? new Date(alert.timestamp_ms).toLocaleTimeString() : 'Unknown Time';
                  const isBlocked = alert.action?.includes('kill') || alert.action?.includes('stop') || alert.action?.includes('quarantine') || alert.action?.includes('terminated_tree') || alert.action?.includes('isolate');
                  const actionLabel = (alert.action || 'NONE').toUpperCase().replace('_', ' ');
                  return (
                    <tr key={i} style={{ background: i % 2 === 0 ? 'var(--hx-overlay-2)' : 'transparent' }}>
                      <td>
                        <span className={`hx-badge hx-badge--${alert.severity}`}>{alert.severity}</span>
                      </td>
                      <td>
                        <span style={{ color: 'var(--hx-text-primary)', fontWeight: 500 }}>{alert.process_name || 'Unknown'}</span>
                        <span style={{ color: 'var(--hx-text-dim)', marginLeft: '8px', fontSize: '12px', fontFamily: 'var(--hx-font-mono)' }}>(PID: {alert.pid || '?'})</span>
                        {alert.quarantine_path && <div style={{ fontSize: '11px', color: 'var(--hx-accent-amber)', fontFamily: 'var(--hx-font-mono)', marginTop: '2px', wordBreak: 'break-all' }}>🗃 {alert.quarantine_path}</div>}
                        {alert.killed_pids && alert.killed_pids.length > 0 && <div style={{ fontSize: '11px', color: 'var(--hx-text-dim)', fontFamily: 'var(--hx-font-mono)', marginTop: '2px' }}>killed: [{alert.killed_pids.join(', ')}]</div>}
                      </td>
                      <td>
                        <span style={{
                          display: 'inline-block', padding: '4px 10px', borderRadius: '4px', fontSize: '11px', fontWeight: 700, letterSpacing: '0.05em',
                          background: isBlocked ? 'rgba(239, 68, 68, 0.1)' : alert.action === 'quarantined' ? 'rgba(245,158,11,0.12)' : 'var(--hx-overlay-5)',
                          color: isBlocked ? 'var(--hx-accent-red)' : alert.action === 'quarantined' ? 'var(--hx-accent-amber)' : 'var(--hx-text-secondary)',
                          border: `1px solid ${isBlocked ? 'rgba(239,68,68,0.2)' : alert.action === 'quarantined' ? 'rgba(245,158,11,0.25)' : 'transparent'}`,
                          boxShadow: isBlocked ? '0 0 10px rgba(239, 68, 68, 0.2)' : 'none'
                        }}>
                          {actionLabel}
                        </span>
                        {alert.threat_score != null && alert.threat_score > 0 && <div style={{ fontSize: '11px', color: 'var(--hx-text-dim)', fontFamily: 'var(--hx-font-mono)', marginTop: '4px' }}>score {alert.threat_score}</div>}
                      </td>
                      <td style={{ fontFamily: 'var(--hx-font-mono)', fontSize: '12px', color: 'var(--hx-text-secondary)' }}>
                        {safeTime}
                      </td>
                    </tr>
                  )
                })}
              </tbody>
            </table>
          </div>
        )}
      </div>
    </div>
  );
}
