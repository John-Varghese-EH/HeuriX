import { jsx as _jsx, jsxs as _jsxs } from "react/jsx-runtime";
import { useState, useEffect } from 'react';
import { invoke } from '@tauri-apps/api/core';
export function ConfigPanel({ theme, setTheme, watchDir, setWatchDir, onRestartEngine, isRestarting, engineStatus }) {
    const [entropy, setEntropy] = useState(7.5);
    const [burstCount, setBurstCount] = useState(15);
    const [burstWindow, setBurstWindow] = useState(2000);
    const [mitigation, setMitigation] = useState('suspend');
    const [autoMitigate, setAutoMitigate] = useState(true);
    const [statusMsg, setStatusMsg] = useState(null);
    // Load config on mount
    useEffect(() => {
        const loadConfig = async () => {
            try {
                const configStr = await invoke('get_config');
                const config = JSON.parse(configStr);
                if (config.watch_dir)
                    setWatchDir(config.watch_dir);
                if (config.entropy_threshold)
                    setEntropy(config.entropy_threshold);
                if (config.burst_count)
                    setBurstCount(config.burst_count);
                if (config.burst_window_ms)
                    setBurstWindow(config.burst_window_ms);
                if (config.mitigation_action)
                    setMitigation(config.mitigation_action);
                if (config.auto_mitigate !== undefined)
                    setAutoMitigate(config.auto_mitigate);
            }
            catch (e) {
                console.error('Failed to load config:', e);
            }
        };
        loadConfig();
    }, []);
    const handleApply = async () => {
        setStatusMsg(null);
        const config = {
            entropy_threshold: entropy,
            burst_count: burstCount,
            burst_window_ms: burstWindow,
            mitigation_action: mitigation,
            auto_mitigate: autoMitigate,
            watch_dir: watchDir
        };
        try {
            await invoke('update_config', { configJson: JSON.stringify(config) });
            setStatusMsg({ type: 'success', text: 'Configuration applied successfully.' });
            setTimeout(() => setStatusMsg(null), 3000);
        }
        catch (e) {
            console.error(e);
            setStatusMsg({ type: 'error', text: `Failed to apply configuration: ${String(e)}` });
        }
    };
    const handleResetDefaults = async () => {
        setEntropy(7.5);
        setBurstCount(15);
        setBurstWindow(2000);
        setMitigation('suspend');
        setAutoMitigate(true);
        setWatchDir('/home/j0x/Documents');
        setStatusMsg({ type: 'success', text: 'Settings reset to defaults. Click "Save Changes" to apply.' });
        setTimeout(() => setStatusMsg(null), 3000);
    };
    return (_jsxs("div", { className: "hx-page-container", children: [_jsxs("div", { className: "hx-header-row", children: [_jsxs("div", { children: [_jsx("h1", { className: "hx-page-title", children: "Settings" }), _jsx("p", { className: "hx-page-desc", children: "Configure detection thresholds and mitigation strategies." })] }), _jsxs("div", { style: { display: 'flex', gap: '12px' }, children: [_jsx("button", { className: "hx-btn", onClick: handleResetDefaults, children: "Reset Defaults" }), _jsx("button", { className: "hx-btn hx-btn--primary", onClick: handleApply, children: "Save Changes" })] })] }), statusMsg && (_jsx("div", { style: {
                    padding: '12px 16px',
                    marginBottom: '24px',
                    borderRadius: '6px',
                    fontSize: '13px',
                    background: statusMsg.type === 'error' ? 'rgba(229, 72, 77, 0.1)' : 'rgba(46, 139, 87, 0.1)',
                    color: statusMsg.type === 'error' ? 'var(--hx-accent-red)' : 'var(--hx-accent-green)',
                    border: `1px solid ${statusMsg.type === 'error' ? 'rgba(229, 72, 77, 0.2)' : 'rgba(46, 139, 87, 0.2)'}`
                }, children: statusMsg.text })), _jsxs("div", { className: "panel", style: { padding: '32px', marginBottom: '24px' }, children: [_jsx("h2", { style: { fontSize: '16px', fontWeight: 600, marginBottom: '24px' }, children: "Theme Appearance" }), _jsx("div", { className: "hx-form-group", children: _jsxs("div", { className: "hx-radio-group", style: { gridTemplateColumns: 'repeat(3, 1fr)' }, children: [_jsxs("div", { className: "hx-radio-card", "data-active": theme === 'system', onClick: () => setTheme('system'), children: [_jsx("div", { className: "hx-radio-title", children: "System" }), _jsx("div", { className: "hx-radio-desc", children: "Match OS preference" })] }), _jsxs("div", { className: "hx-radio-card", "data-active": theme === 'dark', onClick: () => setTheme('dark'), children: [_jsx("div", { className: "hx-radio-title", children: "Dark Mode" }), _jsx("div", { className: "hx-radio-desc", children: "Premium stealth aesthetic" })] }), _jsxs("div", { className: "hx-radio-card", "data-active": theme === 'light', onClick: () => setTheme('light'), children: [_jsx("div", { className: "hx-radio-title", children: "Light Mode" }), _jsx("div", { className: "hx-radio-desc", children: "Clean, bright workspace" })] })] }) })] }), _jsxs("div", { className: "panel", style: { padding: '32px', marginBottom: '24px' }, children: [_jsx("h2", { style: { fontSize: '16px', fontWeight: 600, marginBottom: '24px' }, children: "Monitoring Configuration" }), _jsxs("div", { className: "hx-form-group", children: [_jsx("label", { className: "hx-form-label", children: "Watch Directory" }), _jsx("span", { className: "hx-form-desc", children: "The absolute path to the directory that HeuriX will monitor recursively." }), _jsx("input", { type: "text", className: "hx-input", value: watchDir, onChange: (e) => setWatchDir(e.target.value) })] })] }), _jsxs("div", { className: "panel", style: { padding: '32px', marginBottom: '24px' }, children: [_jsx("h2", { style: { fontSize: '16px', fontWeight: 600, marginBottom: '24px' }, children: "Detection Thresholds" }), _jsxs("div", { className: "hx-form-group", children: [_jsxs("label", { className: "hx-form-label", children: ["Entropy Threshold (", entropy.toFixed(1), ")"] }), _jsx("span", { className: "hx-form-desc", children: "Shannon entropy threshold (0-8). Highly encrypted files approach 8.0." }), _jsx("input", { type: "range", min: "0", max: "8", step: "0.1", className: "hx-slider", value: entropy, onChange: (e) => setEntropy(parseFloat(e.target.value)) })] }), _jsxs("div", { style: { display: 'flex', gap: '24px' }, children: [_jsxs("div", { className: "hx-form-group", style: { flex: 1 }, children: [_jsx("label", { className: "hx-form-label", children: "Burst Count" }), _jsx("span", { className: "hx-form-desc", children: "Number of file mutations required to trigger a burst alert." }), _jsx("input", { type: "number", min: "1", max: "1000", className: "hx-input", value: burstCount, onChange: (e) => setBurstCount(parseInt(e.target.value)) })] }), _jsxs("div", { className: "hx-form-group", style: { flex: 1 }, children: [_jsx("label", { className: "hx-form-label", children: "Burst Window (ms)" }), _jsx("span", { className: "hx-form-desc", children: "Time window in milliseconds for burst detection." }), _jsx("input", { type: "number", min: "100", max: "10000", className: "hx-input", value: burstWindow, onChange: (e) => setBurstWindow(parseInt(e.target.value)) })] })] })] }), _jsxs("div", { className: "panel", style: { padding: '32px', marginBottom: '24px' }, children: [_jsx("h2", { style: { fontSize: '16px', fontWeight: 600, marginBottom: '24px' }, children: "Mitigation Action" }), _jsx("div", { className: "hx-form-group", children: _jsxs("div", { className: "hx-radio-group", style: { gridTemplateColumns: 'repeat(2, 1fr)' }, children: [_jsxs("div", { className: "hx-radio-card", "data-active": mitigation === 'suspend', onClick: () => setMitigation('suspend'), children: [_jsx("div", { className: "hx-radio-title", children: "Suspend Process" }), _jsx("div", { className: "hx-radio-desc", children: "SIGSTOP: pause offending process for manual review." })] }), _jsxs("div", { className: "hx-radio-card", "data-active": mitigation === 'terminate', onClick: () => setMitigation('terminate'), children: [_jsx("div", { className: "hx-radio-title", children: "Terminate Process" }), _jsx("div", { className: "hx-radio-desc", children: "SIGTERM \u2192 SIGKILL: destroy the offending process." })] }), _jsxs("div", { className: "hx-radio-card", "data-active": mitigation === 'quarantine', onClick: () => setMitigation('quarantine'), children: [_jsx("div", { className: "hx-radio-title", children: "Quarantine File" }), _jsx("div", { className: "hx-radio-desc", children: "Move suspect file to .heurix-quarantine (0400) + suspend PID." })] }), _jsxs("div", { className: "hx-radio-card", "data-active": mitigation === 'isolate', onClick: () => setMitigation('isolate'), children: [_jsx("div", { className: "hx-radio-title", children: "Isolate (Tree Kill + Quarantine)" }), _jsx("div", { className: "hx-radio-desc", children: "Kill entire process tree + quarantine file. For critical." })] })] }) }), _jsxs("div", { className: "hx-toggle-wrap", style: { cursor: 'pointer' }, onClick: () => setAutoMitigate(!autoMitigate), children: [_jsxs("div", { className: "hx-toggle-info", children: [_jsx("strong", { children: "Automatic Mitigation" }), _jsx("span", { children: "Automatically execute the selected action when a critical threat is detected." })] }), _jsx("div", { style: {
                                    width: '36px', height: '20px', borderRadius: '10px',
                                    background: autoMitigate ? 'var(--hx-text-primary)' : 'var(--hx-border-hover)',
                                    position: 'relative', transition: 'all 0.2s'
                                }, children: _jsx("div", { style: {
                                        width: '16px', height: '16px', borderRadius: '50%', background: autoMitigate ? '#000' : 'var(--hx-text-secondary)',
                                        position: 'absolute', top: '2px', left: autoMitigate ? '18px' : '2px', transition: 'all 0.2s'
                                    } }) })] })] }), _jsxs("div", { className: "panel", style: { padding: '32px' }, children: [_jsx("h2", { style: { fontSize: '16px', fontWeight: 600, marginBottom: '24px' }, children: "Engine Management" }), _jsxs("div", { className: "hx-form-group", children: [_jsx("label", { className: "hx-form-label", children: "Engine Status" }), _jsxs("div", { style: { display: 'flex', alignItems: 'center', gap: '12px', padding: '16px', background: 'var(--hx-overlay-2)', borderRadius: '8px', border: '1px solid var(--hx-border)' }, children: [_jsx("div", { style: {
                                            width: '12px', height: '12px', borderRadius: '50%',
                                            background: engineStatus === 'running' ? 'var(--hx-accent-green)' : 'var(--hx-accent-red)',
                                            boxShadow: engineStatus === 'running' ? '0 0 8px var(--hx-accent-green)' : 'none'
                                        } }), _jsxs("div", { style: { flex: 1 }, children: [_jsx("div", { style: { fontSize: '14px', fontWeight: 600, color: 'var(--hx-text-primary)' }, children: engineStatus === 'running' ? 'Engine Online' : 'Engine Offline' }), _jsx("div", { style: { fontSize: '12px', color: 'var(--hx-text-secondary)', marginTop: '4px' }, children: engineStatus === 'running' ? 'Monitoring filesystem for threats' : 'Not monitoring - click restart to recover' })] }), _jsx("button", { className: `hx-btn ${engineStatus === 'running' ? 'hx-btn--danger' : 'hx-btn--primary'}`, onClick: onRestartEngine, disabled: isRestarting, style: { minWidth: '100px' }, children: isRestarting ? 'Restarting...' : (engineStatus === 'running' ? 'Restart' : 'Start') })] })] })] })] }));
}
