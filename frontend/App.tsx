import { useState, useEffect, useRef } from 'react';
import { invoke } from '@tauri-apps/api/core';
import { motion, AnimatePresence } from 'framer-motion';
import { Sidebar } from './components/Sidebar';
import { StatusBar } from './components/StatusBar';
import { OverviewHUD, Alert } from './components/OverviewHUD';
import { EventFeed, FeedItem, FsEvent } from './components/EventFeed';
import { ResourceGraph } from './components/ResourceGraph';
import { ConfigPanel } from './components/ConfigPanel';
import { useTauriEvent } from './hooks/useTauriEvent';
import { useEngineStats } from './hooks/useEngineStats';
import { useTheme } from './hooks/useTheme';

export default function App() {
  const [activeTab, setActiveTab] = useState('overview');
  const { theme, setTheme } = useTheme();
  const [status, setStatus] = useState('stopped');
  const [events, setEvents] = useState<FeedItem[]>([]);
  const [alerts, setAlerts] = useState<Alert[]>([]);
  const { stats, latest } = useEngineStats();
  const [uptimeSeconds, setUptimeSeconds] = useState(0);
  const [eventRate, setEventRate] = useState(0);
  const [threatsMitigated, setThreatsMitigated] = useState(0);

  // Shared Configuration State
  const [watchDir, setWatchDir] = useState('/home/j0x/Documents');
  const [isRestarting, setIsRestarting] = useState(false);

  // Real-time Event Counter
  const eventCountRef = useRef(0);
  useEffect(() => {
    const interval = setInterval(() => {
      setEventRate(eventCountRef.current);
      eventCountRef.current = 0;
    }, 1000);
    return () => clearInterval(interval);
  }, []);

  // Real Engine Uptime Tracking
  const engineStartTimeRef = useRef<number | null>(null);
  useEffect(() => {
    if (status === 'running') {
      if (!engineStartTimeRef.current) engineStartTimeRef.current = Date.now();
      const interval = setInterval(() => {
        setUptimeSeconds(Math.floor((Date.now() - engineStartTimeRef.current!) / 1000));
      }, 1000);
      return () => clearInterval(interval);
    } else {
      engineStartTimeRef.current = null;
      setUptimeSeconds(0);
    }
  }, [status]);

  useTauriEvent<{type: string, data: FsEvent}>('heurix://fs-event', (payload) => {
    const fsData = payload.data;
    eventCountRef.current++;
    setEvents(prev => {
      const next = [...prev, { type: 'fs' as const, data: fsData }];
      return next.length > 500 ? next.slice(next.length - 500) : next;
    });
  });

  useTauriEvent<{type: string, data: Alert}>('heurix://alert', (payload) => {
    const alertData = payload.data;
    setEvents(prev => {
      const next = [...prev, { type: 'alert' as const, data: alertData }];
      return next.length > 500 ? next.slice(next.length - 500) : next;
    });
    setAlerts(prev => {
      const next = [...prev, alertData];
      return next.length > 100 ? next.slice(next.length - 100) : next;
    });
    if (alertData.action && alertData.action !== 'none') {
      setThreatsMitigated(t => t + 1);
    }
  });

  useTauriEvent<{type: string, data: {status: string}}>('heurix://status', (payload) => {
    const newStatus = payload.data.status;
    if (newStatus === 'started' || newStatus === 'running') {
      setStatus('running');
    } else if (newStatus === 'stopped' || newStatus === 'terminated') {
      setStatus('stopped');
    } else if (newStatus === 'heartbeat') {
      // Update last heartbeat time
    } else if (newStatus === 'config_updated') {
      // Config was updated
    }
  });

  useEffect(() => {
    invoke<boolean>('get_engine_status')
      .then(res => setStatus(res ? 'running' : 'stopped'))
      .catch(e => console.error(e));
  }, []);

  const formatUptime = (secs: number) => {
    const h = Math.floor(secs / 3600);
    const m = Math.floor((secs % 3600) / 60);
    const s = secs % 60;
    return `${h.toString().padStart(2, '0')}:${m.toString().padStart(2, '0')}:${s.toString().padStart(2, '0')}`;
  };

  const clearEvents = () => setEvents([]);

  const handleRestartEngine = async () => {
    setIsRestarting(true);
    try {
      await invoke('restart_engine');
      setStatus('running');
    } catch (e) {
      console.error('Failed to restart engine:', e);
      setStatus('stopped');
    }
    setIsRestarting(false);
  };

  return (
    <div className="hx-app">
      <Sidebar activeTab={activeTab} setActiveTab={setActiveTab} status={status} />

      <div className="hx-main">
        <AnimatePresence mode="wait">
          {activeTab === 'overview' && (
            <motion.div key="overview" initial={{ opacity: 0 }} animate={{ opacity: 1 }} exit={{ opacity: 0 }} transition={{ duration: 0.15 }}>
              <OverviewHUD
                alerts={alerts}
                threatsMitigated={threatsMitigated}
                targetPath={watchDir}
                eventRate={eventRate}
                uptime={formatUptime(uptimeSeconds)}
                events={events}
                engineStatus={status}
              />
            </motion.div>
          )}
          {activeTab === 'monitor' && (
            <motion.div key="monitor" initial={{ opacity: 0 }} animate={{ opacity: 1 }} exit={{ opacity: 0 }} transition={{ duration: 0.15 }} style={{ height: '100%' }}>
              <EventFeed events={events} onClear={clearEvents} />
            </motion.div>
          )}
          {activeTab === 'resources' && (
            <motion.div key="resources" initial={{ opacity: 0 }} animate={{ opacity: 1 }} exit={{ opacity: 0 }} transition={{ duration: 0.15 }}>
              <ResourceGraph stats={stats} latest={latest} />
            </motion.div>
          )}
          {activeTab === 'config' && (
            <motion.div key="config" initial={{ opacity: 0 }} animate={{ opacity: 1 }} exit={{ opacity: 0 }} transition={{ duration: 0.15 }}>
              <ConfigPanel
                theme={theme}
                setTheme={setTheme}
                watchDir={watchDir}
                setWatchDir={setWatchDir}
                onRestartEngine={handleRestartEngine}
                isRestarting={isRestarting}
                engineStatus={status}
              />
            </motion.div>
          )}
        </AnimatePresence>
      </div>

      <StatusBar status={status} eventRate={eventRate} uptime={formatUptime(uptimeSeconds)} />
    </div>
  );
}