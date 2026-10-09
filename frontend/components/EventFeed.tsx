import { useRef, useEffect, useState, useMemo } from 'react';
import { Terminal, Lock, Unlock, Trash2, Search, FileEdit, FilePlus, FileMinus, FileText, ShieldAlert } from 'lucide-react';
import { Alert } from './OverviewHUD';

export interface FsEvent {
  path: string;
  event_type: 'CREATE' | 'MODIFY' | 'DELETE' | 'RENAME';
  timestamp_ms: number;
}

export type FeedItem = 
  | { type: 'fs', data: FsEvent }
  | { type: 'alert', data: Alert };

interface EventFeedProps {
  events: FeedItem[];
  onClear: () => void;
}

export function EventFeed({ events, onClear }: EventFeedProps) {
  const [autoScroll, setAutoScroll] = useState(true);
  const [search, setSearch] = useState('');
  const endRef = useRef<HTMLDivElement>(null);

  const filteredEvents = useMemo(() => {
    if (!search) return events;
    return events.filter(item => {
      const s = search.toLowerCase();
      if (item.type === 'fs') {
        return item.data.path.toLowerCase().includes(s) || item.data.event_type.toLowerCase().includes(s);
      } else {
        return item.data.process_name.toLowerCase().includes(s) || item.data.action?.toLowerCase().includes(s);
      }
    });
  }, [events, search]);

  useEffect(() => {
    if (autoScroll && endRef.current) {
      endRef.current.scrollIntoView({ behavior: 'auto' });
    }
  }, [filteredEvents, autoScroll]);

  const getEventIcon = (type: string) => {
    switch (type) {
      case 'CREATE': return <FilePlus size={14} color="var(--hx-accent-green)" />;
      case 'MODIFY': return <FileEdit size={14} color="var(--hx-accent-blue)" />;
      case 'DELETE': return <FileMinus size={14} color="var(--hx-accent-red)" />;
      case 'RENAME': return <FileText size={14} color="var(--hx-accent-amber)" />;
      default: return <FileText size={14} />;
    }
  };

  const getEventColor = (type: string) => {
    switch (type) {
      case 'CREATE': return 'var(--hx-accent-green)';
      case 'MODIFY': return 'var(--hx-accent-blue)';
      case 'DELETE': return 'var(--hx-accent-red)';
      case 'RENAME': return 'var(--hx-accent-amber)';
      default: return 'var(--hx-text-primary)';
    }
  };

  return (
    <div className="hx-log-viewer panel" style={{ display: 'flex', flexDirection: 'column', height: '100%' }}>
      <div className="hx-log-header" style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', padding: '16px 24px', borderBottom: '1px solid var(--hx-border)', background: 'var(--hx-bg-header)' }}>
        <div style={{ display: 'flex', alignItems: 'center', gap: '8px', fontSize: '14px', fontWeight: 600, color: 'var(--hx-text-primary)', letterSpacing: '0.05em', textTransform: 'uppercase' }}>
          <Terminal size={16} color="var(--hx-accent-blue)" /> Raw Telemetry Feed
        </div>
        
        <div style={{ display: 'flex', gap: '16px', alignItems: 'center' }}>
          <div style={{ position: 'relative' }}>
            <Search size={14} color="var(--hx-text-dim)" style={{ position: 'absolute', left: '10px', top: '50%', transform: 'translateY(-50%)' }} />
            <input 
              type="text" 
              placeholder="Search paths or processes..." 
              value={search}
              onChange={(e) => setSearch(e.target.value)}
              style={{
                background: 'rgba(0,0,0,0.2)',
                border: '1px solid var(--hx-border)',
                borderRadius: '6px',
                padding: '6px 12px 6px 30px',
                color: 'var(--hx-text-primary)',
                fontSize: '13px',
                width: '240px',
                outline: 'none',
                transition: 'border-color 0.2s',
              }}
            />
          </div>
          
          <div style={{ height: '24px', width: '1px', background: 'var(--hx-border)' }} />

          <div style={{ display: 'flex', gap: '8px' }}>
            <button className={`hx-btn ${autoScroll ? 'hx-btn--primary' : ''}`} style={{ padding: '6px 12px', fontSize: '12px' }} onClick={() => setAutoScroll(!autoScroll)}>
              {autoScroll ? <Lock size={12} /> : <Unlock size={12} />}
              {autoScroll ? 'Auto-scroll' : 'Paused'}
            </button>
            <button className="hx-btn" style={{ padding: '6px 12px', fontSize: '12px' }} onClick={onClear}>
              <Trash2 size={12} /> Clear
            </button>
          </div>
        </div>
      </div>
      
      <div className="hx-log-content" style={{ flex: 1, overflowY: 'auto', padding: '12px', fontFamily: 'var(--hx-font-mono)' }}>
        <div style={{ display: 'grid', gridTemplateColumns: '120px 120px 1fr', gap: '16px', padding: '8px 16px', borderBottom: '1px solid var(--hx-border)', color: 'var(--hx-text-dim)', fontSize: '11px', fontWeight: 600, textTransform: 'uppercase', letterSpacing: '0.05em' }}>
          <div>Timestamp</div>
          <div>Event Type</div>
          <div>Target / Details</div>
        </div>

        {filteredEvents.length === 0 ? (
          <div style={{ padding: '48px', textAlign: 'center', color: 'var(--hx-text-dim)' }}>
            <Terminal size={32} style={{ opacity: 0.2, margin: '0 auto 16px' }} />
            {search ? 'No telemetry events match your search.' : 'Awaiting real-time telemetry stream...'}
          </div>
        ) : (
          filteredEvents.map((item, i) => {
            if (item.type === 'fs') {
              const { path, event_type, timestamp_ms } = item.data;
              const safeTime = timestamp_ms ? new Date(timestamp_ms).toISOString().substring(11, 23) : 'Unknown Time';
              return (
                <div key={i} style={{ display: 'grid', gridTemplateColumns: '120px 120px 1fr', gap: '16px', padding: '10px 16px', borderBottom: '1px solid rgba(255,255,255,0.02)', fontSize: '13px', alignItems: 'center' }}>
                  <span style={{ color: 'var(--hx-text-dim)' }}>{safeTime}</span>
                  <span style={{ color: getEventColor(event_type.toUpperCase()), display: 'flex', alignItems: 'center', gap: '6px', fontWeight: 500 }}>
                    {getEventIcon(event_type.toUpperCase())} {event_type.toUpperCase()}
                  </span>
                  <span style={{ color: 'var(--hx-text-secondary)', whiteSpace: 'nowrap', overflow: 'hidden', textOverflow: 'ellipsis' }}>
                    {path}
                  </span>
                </div>
              );
            } else {
              const { process_name, action, timestamp_ms, entropy, pid } = item.data;
              const isBlocked = action?.includes('kill') || action?.includes('stop') || action?.includes('quarantine') || action?.includes('terminated_tree') || action?.includes('isolate');
              const safeTime = timestamp_ms ? new Date(timestamp_ms).toISOString().substring(11, 23) : 'Unknown Time';
              return (
                <div key={i} style={{ display: 'grid', gridTemplateColumns: '120px 120px 1fr', gap: '16px', padding: '12px 16px', margin: '4px 0', background: 'rgba(239, 68, 68, 0.05)', borderLeft: '3px solid var(--hx-accent-red)', borderRadius: '4px', fontSize: '13px', alignItems: 'center' }}>
                  <span style={{ color: 'var(--hx-text-dim)' }}>{safeTime}</span>
                  <span style={{ color: 'var(--hx-accent-red)', display: 'flex', alignItems: 'center', gap: '6px', fontWeight: 600 }}>
                    <ShieldAlert size={14} /> THREAT
                  </span>
                  <span style={{ display: 'flex', alignItems: 'center', gap: '12px', flexWrap: 'wrap' }}>
                    <span style={{ color: 'var(--hx-text-primary)', fontWeight: 600 }}>{process_name || 'Unknown'}</span> 
                    <span style={{ color: 'var(--hx-text-dim)', fontSize: '11px', background: 'rgba(255,255,255,0.05)', padding: '2px 6px', borderRadius: '4px' }}>PID: {pid || '?'}</span>
                    <span style={{ color: 'var(--hx-accent-amber)', fontSize: '12px' }}>Entropy: {entropy?.toFixed(2) || 'N/A'}</span>
                    <span style={{ color: 'var(--hx-text-dim)' }}>&rarr;</span>
                    <span style={{ color: isBlocked ? 'var(--hx-accent-red)' : 'var(--hx-accent-blue)', fontWeight: 600, fontSize: '12px', padding: '2px 8px', borderRadius: '4px', background: isBlocked ? 'rgba(239, 68, 68, 0.1)' : 'rgba(59, 130, 246, 0.1)' }}>
                      {action?.toUpperCase() || 'UNKNOWN'}
                    </span>
                  </span>
                </div>
              );
            }
          })
        )}
        <div ref={endRef} />
      </div>
    </div>
  );
}
