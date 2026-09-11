import { useState, useMemo } from 'react';
import { AreaChart, Area, XAxis, YAxis, Tooltip, ResponsiveContainer, CartesianGrid, LineChart, Line } from 'recharts';
import { Cpu, Database, Activity, Maximize2, HardDrive } from 'lucide-react';
import { SystemStats } from '../hooks/useEngineStats';

interface ResourceGraphProps {
  stats: SystemStats[];
  latest: SystemStats | null;
}

type TimeRange = 'live' | '1m' | '5m';

export function ResourceGraph({ stats, latest }: ResourceGraphProps) {
  const [timeRange, setTimeRange] = useState<TimeRange>('live');

  // Filter stats based on time ranges
  const filteredStats = useMemo(() => {
    let limit = 300;
    if (timeRange === '1m') limit = 300;
    if (timeRange === 'live') limit = 100;

    const data = stats.slice(-limit);
    return data.map((s, i) => {
      const cpu = s.cpu_percent;
      const mem = s.mem_percent;
      const io_read = s.io_read_mb;
      const io_write = s.io_write_mb;
      // Calculate total I/O rate
      const io = io_read + io_write;

      return {
        time: i,
        cpu,
        mem,
        io,
        io_read,
        io_write
      };
    });
  }, [stats, timeRange]);

  // Calculate aggregates
  const aggregates = useMemo(() => {
    if (filteredStats.length === 0) return { cpuAvg: '0.0', memAvg: '0.0', cpuMax: '0.0', ioPeak: 0, ioTotal: 0 };
    const cpuSum = filteredStats.reduce((acc, curr) => acc + curr.cpu, 0);
    const memSum = filteredStats.reduce((acc, curr) => acc + curr.mem, 0);
    const cpuMax = Math.max(...filteredStats.map(s => s.cpu));
    const ioPeak = Math.max(...filteredStats.map(s => s.io));
    const ioTotal = filteredStats.reduce((acc, curr) => acc + curr.io, 0);
    return {
      cpuAvg: (cpuSum / filteredStats.length).toFixed(1),
      memAvg: (memSum / filteredStats.length).toFixed(1),
      cpuMax: cpuMax.toFixed(1),
      ioPeak: ioPeak.toFixed(1),
      ioTotal: ioTotal.toFixed(1)
    };
  }, [filteredStats]);

  const CustomTooltip = ({ active, payload }: any) => {
    if (active && payload && payload.length) {
      return (
        <div style={{
          background: 'var(--hx-bg-tooltip, rgba(10, 10, 10, 0.9))',
          border: '1px solid var(--hx-border)',
          borderRadius: '8px',
          padding: '12px',
          boxShadow: '0 8px 32px rgba(0,0,0,0.15)',
          backdropFilter: 'blur(10px)',
          minWidth: '160px'
        }}>
          <p style={{ color: 'var(--hx-text-secondary)', fontSize: '11px', marginBottom: '10px', fontWeight: 600, letterSpacing: '0.05em', textTransform: 'uppercase' }}>T-{((filteredStats.length - payload[0].payload.time) * 0.1).toFixed(1)}s</p>
          {payload.map((entry: any, index: number) => (
            <div key={index} style={{ display: 'flex', justifyContent: 'space-between', marginBottom: '6px', fontSize: '13px', alignItems: 'center' }}>
              <span style={{ color: entry.color, fontWeight: 500, display: 'flex', alignItems: 'center', gap: '6px' }}>
                <div style={{ width: 8, height: 8, borderRadius: '50%', background: entry.color }} />
                {entry.name}
              </span>
              <span style={{ color: 'var(--hx-text-primary)', fontWeight: 600, fontFamily: 'var(--hx-font-mono)' }}>
                {entry.value.toFixed(1)}{entry.name === 'I/O' ? ' MB/s' : '%'}
              </span>
            </div>
          ))}
        </div>
      );
    }
    return null;
  };

  return (
    <div className="hx-page-container">
      <div className="hx-header-row" style={{ marginBottom: '24px' }}>
        <div>
          <h1 className="hx-page-title">System Resources</h1>
          <p className="hx-page-desc">High-fidelity host telemetry and performance analytics.</p>
        </div>
        <div style={{ display: 'flex', gap: '8px', background: 'var(--hx-border)', padding: '4px', borderRadius: '8px', border: '1px solid var(--hx-border)' }}>
          <button
            className={`hx-btn ${timeRange === 'live' ? 'hx-btn--primary' : ''}`}
            style={{ padding: '6px 12px', border: 'none', boxShadow: 'none' }}
            onClick={() => setTimeRange('live')}
          >Live (10s)</button>
          <button
            className={`hx-btn ${timeRange === '1m' ? 'hx-btn--primary' : ''}`}
            style={{ padding: '6px 12px', border: 'none', boxShadow: 'none' }}
            onClick={() => setTimeRange('1m')}
          >30s</button>
        </div>
      </div>

      {/* Aggregate Stats Row with Sparklines */}
      <div style={{ display: 'grid', gridTemplateColumns: 'repeat(auto-fit, minmax(200px, 1fr))', gap: '16px', marginBottom: '24px' }}>

        {/* CPU Avg */}
        <div className="panel" style={{ padding: '16px', display: 'flex', flexDirection: 'column', gap: '4px', position: 'relative', overflow: 'hidden' }}>
          <span style={{ fontSize: '12px', color: 'var(--hx-text-secondary)', display: 'flex', alignItems: 'center', gap: '6px', position: 'relative', zIndex: 2 }}><Activity size={12} color="var(--hx-accent-blue)"/> CPU Avg</span>
          <span style={{ fontSize: '24px', fontWeight: 600, color: 'var(--hx-text-primary)', fontFamily: 'var(--hx-font-mono)', position: 'relative', zIndex: 2 }}>{aggregates.cpuAvg}%</span>
          <div style={{ position: 'absolute', bottom: 0, left: 0, right: 0, height: '40px', opacity: 0.3, zIndex: 1 }}>
            <ResponsiveContainer width="100%" height="100%">
              <LineChart data={filteredStats}>
                <Line type="monotone" dataKey="cpu" stroke="var(--hx-accent-blue)" strokeWidth={2} dot={false} isAnimationActive={false} />
              </LineChart>
            </ResponsiveContainer>
          </div>
        </div>

        {/* I/O Peak */}
        <div className="panel" style={{ padding: '16px', display: 'flex', flexDirection: 'column', gap: '4px', position: 'relative', overflow: 'hidden' }}>
          <span style={{ fontSize: '12px', color: 'var(--hx-text-secondary)', display: 'flex', alignItems: 'center', gap: '6px', position: 'relative', zIndex: 2 }}><HardDrive size={12} color="var(--hx-accent-amber)"/> I/O Peak</span>
          <span style={{ fontSize: '24px', fontWeight: 600, color: 'var(--hx-text-primary)', fontFamily: 'var(--hx-font-mono)', position: 'relative', zIndex: 2 }}>{aggregates.ioPeak}<span style={{fontSize: '12px', color: 'var(--hx-text-dim)'}}> MB/s</span></span>
          <div style={{ position: 'absolute', bottom: 0, left: 0, right: 0, height: '40px', opacity: 0.3, zIndex: 1 }}>
            <ResponsiveContainer width="100%" height="100%">
              <LineChart data={filteredStats}>
                <Line type="monotone" dataKey="io" stroke="var(--hx-accent-amber)" strokeWidth={2} dot={false} isAnimationActive={false} />
              </LineChart>
            </ResponsiveContainer>
          </div>
        </div>

        {/* Mem Avg */}
        <div className="panel" style={{ padding: '16px', display: 'flex', flexDirection: 'column', gap: '4px', position: 'relative', overflow: 'hidden' }}>
          <span style={{ fontSize: '12px', color: 'var(--hx-text-secondary)', display: 'flex', alignItems: 'center', gap: '6px', position: 'relative', zIndex: 2 }}><Database size={12} color="var(--hx-accent-green)"/> Mem Avg</span>
          <span style={{ fontSize: '24px', fontWeight: 600, color: 'var(--hx-text-primary)', fontFamily: 'var(--hx-font-mono)', position: 'relative', zIndex: 2 }}>{aggregates.memAvg}%</span>
          <div style={{ position: 'absolute', bottom: 0, left: 0, right: 0, height: '40px', opacity: 0.3, zIndex: 1 }}>
            <ResponsiveContainer width="100%" height="100%">
              <LineChart data={filteredStats}>
                <Line type="monotone" dataKey="mem" stroke="var(--hx-accent-green)" strokeWidth={2} dot={false} isAnimationActive={false} />
              </LineChart>
            </ResponsiveContainer>
          </div>
        </div>

        {/* Mem Total */}
        <div className="panel" style={{ padding: '16px', display: 'flex', flexDirection: 'column', gap: '4px', position: 'relative', overflow: 'hidden' }}>
          <span style={{ fontSize: '12px', color: 'var(--hx-text-secondary)', display: 'flex', alignItems: 'center', gap: '6px' }}><Maximize2 size={12}/> System Memory</span>
          <span style={{ fontSize: '24px', fontWeight: 600, color: 'var(--hx-text-primary)', fontFamily: 'var(--hx-font-mono)' }}>
            {latest && latest.mem_total_mb !== undefined ? (latest.mem_total_mb / 1024).toFixed(1) : '0'} GB
          </span>
        </div>
      </div>

      <div style={{ display: 'grid', gridTemplateColumns: 'repeat(2, 1fr)', gap: '24px', marginBottom: '24px' }}>

        {/* CPU Chart */}
        <div className="panel" style={{ padding: '24px', position: 'relative', overflow: 'hidden' }}>
          <div style={{ position: 'absolute', top: 0, left: 0, right: 0, height: '1px', background: 'linear-gradient(90deg, transparent, var(--hx-accent-blue), transparent)', opacity: 0.5 }} />
          <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: '24px' }}>
            <div style={{ display: 'flex', alignItems: 'center', gap: '8px', fontWeight: 500 }}>
              <Cpu size={16} color="var(--hx-accent-blue)" /> CPU Utilization
            </div>
            <div style={{ fontSize: '24px', fontWeight: 600, fontFamily: 'var(--hx-font-mono)', color: 'var(--hx-accent-blue)', textShadow: '0 0 20px rgba(59, 130, 246, 0.4)' }}>
              {latest && latest.cpu_percent !== undefined ? latest.cpu_percent.toFixed(1) : '0.0'}%
            </div>
          </div>
          <div style={{ height: 200 }}>
            <ResponsiveContainer width="100%" height="100%">
              <AreaChart data={filteredStats} margin={{ top: 10, right: 0, left: -20, bottom: 0 }}>
                <defs>
                  <linearGradient id="colorCpu" x1="0" y1="0" x2="0" y2="1">
                    <stop offset="5%" stopColor="var(--hx-accent-blue)" stopOpacity={0.4}/>
                    <stop offset="95%" stopColor="var(--hx-accent-blue)" stopOpacity={0}/>
                  </linearGradient>
                </defs>
                <CartesianGrid strokeDasharray="3 3" vertical={false} stroke="var(--hx-border)" />
                <XAxis dataKey="time" hide />
                <YAxis domain={[0, 100]} stroke="transparent" tick={{ fill: 'var(--hx-text-secondary)', fontSize: 11 }} tickLine={false} axisLine={false} />
                <Tooltip content={<CustomTooltip />} cursor={{ stroke: 'var(--hx-border)', strokeWidth: 1, strokeDasharray: '4 4' }} />
                <Area type="monotone" name="CPU" dataKey="cpu" stroke="var(--hx-accent-blue)" strokeWidth={2.5} fillOpacity={1} fill="url(#colorCpu)" isAnimationActive={false} activeDot={{ r: 5, fill: 'var(--hx-bg-panel)', stroke: 'var(--hx-accent-blue)', strokeWidth: 2 }} />
              </AreaChart>
            </ResponsiveContainer>
          </div>
        </div>

        {/* Memory & I/O Chart */}
        <div className="panel" style={{ padding: '24px', position: 'relative', overflow: 'hidden' }}>
          <div style={{ position: 'absolute', top: 0, left: 0, right: 0, height: '1px', background: 'linear-gradient(90deg, transparent, var(--hx-accent-green), transparent)', opacity: 0.5 }} />
          <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: '24px' }}>
            <div style={{ display: 'flex', alignItems: 'center', gap: '24px' }}>
              <div style={{ display: 'flex', alignItems: 'center', gap: '8px', fontWeight: 500 }}>
                <Database size={16} color="var(--hx-accent-green)" /> Memory Usage
              </div>
            </div>
            <div style={{ display: 'flex', alignItems: 'baseline', gap: '8px' }}>
              <div style={{ fontSize: '24px', fontWeight: 600, fontFamily: 'var(--hx-font-mono)', color: 'var(--hx-accent-green)', textShadow: '0 0 20px rgba(16, 185, 129, 0.4)' }}>
                {latest && latest.mem_percent !== undefined ? latest.mem_percent.toFixed(1) : '0.0'}%
              </div>
            </div>
          </div>
          <div style={{ height: 200 }}>
            <ResponsiveContainer width="100%" height="100%">
              <AreaChart data={filteredStats} margin={{ top: 10, right: 0, left: -20, bottom: 0 }}>
                <defs>
                  <linearGradient id="colorMem" x1="0" y1="0" x2="0" y2="1">
                    <stop offset="5%" stopColor="var(--hx-accent-green)" stopOpacity={0.4}/>
                    <stop offset="95%" stopColor="var(--hx-accent-green)" stopOpacity={0}/>
                  </linearGradient>
                  <linearGradient id="colorIo" x1="0" y1="0" x2="0" y2="1">
                    <stop offset="5%" stopColor="var(--hx-accent-amber)" stopOpacity={0.2}/>
                    <stop offset="95%" stopColor="var(--hx-accent-amber)" stopOpacity={0}/>
                  </linearGradient>
                </defs>
                <CartesianGrid strokeDasharray="3 3" vertical={false} stroke="var(--hx-border)" />
                <XAxis dataKey="time" hide />
                <YAxis domain={[0, 100]} stroke="transparent" tick={{ fill: 'var(--hx-text-secondary)', fontSize: 11 }} tickLine={false} axisLine={false} />
                <Tooltip content={<CustomTooltip />} cursor={{ stroke: 'var(--hx-border)', strokeWidth: 1, strokeDasharray: '4 4' }} />

                <Area type="monotone" name="I/O" dataKey="io" stroke="var(--hx-accent-amber)" strokeWidth={1.5} fillOpacity={1} fill="url(#colorIo)" isAnimationActive={false} activeDot={{ r: 4, fill: 'var(--hx-bg-panel)', stroke: 'var(--hx-accent-amber)' }} />
                <Area type="monotone" name="Memory" dataKey="mem" stroke="var(--hx-accent-green)" strokeWidth={2.5} fillOpacity={1} fill="url(#colorMem)" isAnimationActive={false} activeDot={{ r: 5, fill: 'var(--hx-bg-panel)', stroke: 'var(--hx-accent-green)', strokeWidth: 2 }} />
              </AreaChart>
            </ResponsiveContainer>
          </div>
        </div>

      </div>

      {/* I/O Rate Full Width Chart */}
      <div className="panel" style={{ padding: '24px', position: 'relative', overflow: 'hidden', flex: 1, minHeight: '300px' }}>
        <div style={{ position: 'absolute', top: 0, left: 0, right: 0, height: '1px', background: 'linear-gradient(90deg, transparent, var(--hx-accent-amber), transparent)', opacity: 0.5 }} />
        <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: '24px' }}>
          <div style={{ display: 'flex', alignItems: 'center', gap: '8px', fontWeight: 500 }}>
            <HardDrive size={16} color="var(--hx-accent-amber)" /> Disk I/O Rate
          </div>
          <div style={{ display: 'flex', alignItems: 'baseline', gap: '8px' }}>
            <div style={{ fontSize: '24px', fontWeight: 600, fontFamily: 'var(--hx-font-mono)', color: 'var(--hx-accent-amber)', textShadow: '0 0 20px rgba(245, 158, 11, 0.4)' }}>
              {filteredStats.length > 0 ? filteredStats[filteredStats.length - 1].io.toFixed(1) : '0.0'}
            </div>
            <div style={{ fontSize: '13px', color: 'var(--hx-text-secondary)' }}>MB/s</div>
          </div>
        </div>
        <div style={{ height: 'calc(100% - 60px)' }}>
          <ResponsiveContainer width="100%" height="100%">
            <AreaChart data={filteredStats} margin={{ top: 10, right: 0, left: -20, bottom: 0 }}>
              <defs>
                <linearGradient id="colorIoRate" x1="0" y1="0" x2="0" y2="1">
                  <stop offset="5%" stopColor="var(--hx-accent-amber)" stopOpacity={0.4}/>
                  <stop offset="95%" stopColor="var(--hx-accent-amber)" stopOpacity={0}/>
                </linearGradient>
              </defs>
              <CartesianGrid strokeDasharray="3 3" vertical={false} stroke="var(--hx-border)" />
              <XAxis dataKey="time" hide />
              <YAxis domain={['auto', 'auto']} stroke="transparent" tick={{ fill: 'var(--hx-text-secondary)', fontSize: 11 }} tickLine={false} axisLine={false} />
              <Tooltip content={<CustomTooltip />} cursor={{ stroke: 'var(--hx-border)', strokeWidth: 1, strokeDasharray: '4 4' }} />
              <Area type="monotone" name="I/O" dataKey="io" stroke="var(--hx-accent-amber)" strokeWidth={2.5} fillOpacity={1} fill="url(#colorIoRate)" isAnimationActive={false} activeDot={{ r: 5, fill: 'var(--hx-bg-panel)', stroke: 'var(--hx-accent-amber)', strokeWidth: 2 }} />
            </AreaChart>
          </ResponsiveContainer>
        </div>
      </div>
    </div>
  );
}