import { jsx as _jsx, jsxs as _jsxs } from "react/jsx-runtime";
import { useState, useEffect, useRef } from 'react';
import { invoke } from '@tauri-apps/api/core';
import { motion, AnimatePresence } from 'framer-motion';
import { Sidebar } from './components/Sidebar';
import { StatusBar } from './components/StatusBar';
import { OverviewHUD } from './components/OverviewHUD';
import { EventFeed } from './components/EventFeed';
import { ResourceGraph } from './components/ResourceGraph';
import { ConfigPanel } from './components/ConfigPanel';
import { useTauriEvent } from './hooks/useTauriEvent';
import { useEngineStats } from './hooks/useEngineStats';
import { useTheme } from './hooks/useTheme';
export default function App() {
    const [activeTab, setActiveTab] = useState('overview');
    const { theme, setTheme } = useTheme();
    const [status, setStatus] = useState('stopped');
    const [events, setEvents] = useState([]);
    const [alerts, setAlerts] = useState([]);
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
    const engineStartTimeRef = useRef(null);
    useEffect(() => {
        if (status === 'running') {
            if (!engineStartTimeRef.current)
                engineStartTimeRef.current = Date.now();
            const interval = setInterval(() => {
                setUptimeSeconds(Math.floor((Date.now() - engineStartTimeRef.current) / 1000));
            }, 1000);
            return () => clearInterval(interval);
        }
        else {
            engineStartTimeRef.current = null;
            setUptimeSeconds(0);
        }
    }, [status]);
    useTauriEvent('heurix://fs-event', (payload) => {
        const fsData = payload.data;
        eventCountRef.current++;
        setEvents(prev => {
            const next = [...prev, { type: 'fs', data: fsData }];
            return next.length > 500 ? next.slice(next.length - 500) : next;
        });
    });
    useTauriEvent('heurix://alert', (payload) => {
        const alertData = payload.data;
        setEvents(prev => {
            const next = [...prev, { type: 'alert', data: alertData }];
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
    useTauriEvent('heurix://status', (payload) => {
        const newStatus = payload.data.status;
        if (newStatus === 'started' || newStatus === 'running' || newStatus === 'connected') {
            setStatus('running');
        }
        else if (newStatus === 'stopped' || newStatus === 'terminated' || newStatus === 'disconnected') {
            setStatus('stopped');
        }
        else if (newStatus === 'heartbeat') {
            // Update last heartbeat time
        }
        else if (newStatus === 'config_updated') {
            // Config was updated
        }
    });
    useEffect(() => {
        invoke('get_engine_status')
            .then(res => setStatus(res ? 'running' : 'stopped'))
            .catch(e => console.error(e));
    }, []);
    const formatUptime = (secs) => {
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
        }
        catch (e) {
            console.error('Failed to restart engine:', e);
            setStatus('stopped');
        }
        setIsRestarting(false);
    };
    return (_jsxs("div", { className: "hx-app", children: [_jsx(Sidebar, { activeTab: activeTab, setActiveTab: setActiveTab, status: status }), _jsx("div", { className: "hx-main", children: _jsxs(AnimatePresence, { mode: "wait", children: [activeTab === 'overview' && (_jsx(motion.div, { initial: { opacity: 0 }, animate: { opacity: 1 }, exit: { opacity: 0 }, transition: { duration: 0.15 }, children: _jsx(OverviewHUD, { alerts: alerts, threatsMitigated: threatsMitigated, targetPath: watchDir, eventRate: eventRate, uptime: formatUptime(uptimeSeconds), events: events, engineStatus: status }) }, "overview")), activeTab === 'monitor' && (_jsx(motion.div, { initial: { opacity: 0 }, animate: { opacity: 1 }, exit: { opacity: 0 }, transition: { duration: 0.15 }, style: { height: '100%' }, children: _jsx(EventFeed, { events: events, onClear: clearEvents }) }, "monitor")), activeTab === 'resources' && (_jsx(motion.div, { initial: { opacity: 0 }, animate: { opacity: 1 }, exit: { opacity: 0 }, transition: { duration: 0.15 }, children: _jsx(ResourceGraph, { stats: stats, latest: latest }) }, "resources")), activeTab === 'config' && (_jsx(motion.div, { initial: { opacity: 0 }, animate: { opacity: 1 }, exit: { opacity: 0 }, transition: { duration: 0.15 }, children: _jsx(ConfigPanel, { theme: theme, setTheme: setTheme, watchDir: watchDir, setWatchDir: setWatchDir, onRestartEngine: handleRestartEngine, isRestarting: isRestarting, engineStatus: status }) }, "config"))] }) }), _jsx(StatusBar, { status: status, eventRate: eventRate, uptime: formatUptime(uptimeSeconds) })] }));
}
