import { jsx as _jsx, jsxs as _jsxs } from "react/jsx-runtime";
export function StatusBar({ status, eventRate, uptime }) {
    const isRunning = status === 'running';
    const isError = status === 'error' || status === 'critical';
    return (_jsxs("div", { className: "hx-statusbar", children: [_jsxs("div", { style: { display: 'flex', alignItems: 'center', gap: '8px' }, children: [_jsx("div", { className: `hx-status-dot ${isRunning ? 'hx-status-dot--running' : isError ? 'hx-status-dot--error' : ''}` }), _jsx("span", { style: { fontWeight: 500, color: isRunning ? 'var(--hx-text-primary)' : 'var(--hx-text-secondary)' }, children: status.charAt(0).toUpperCase() + status.slice(1) })] }), _jsx("div", { children: _jsxs("span", { style: { color: 'var(--hx-text-secondary)' }, children: [eventRate, " events/sec"] }) }), _jsxs("div", { style: { display: 'flex', gap: '16px' }, children: [_jsxs("span", { children: ["Uptime: ", uptime] }), _jsx("span", { style: { color: 'var(--hx-border-focus)' }, children: "|" }), _jsx("span", { style: { color: 'var(--hx-text-dim)' }, children: "v0.1.0" })] })] }));
}
