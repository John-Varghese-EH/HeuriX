import { jsx as _jsx, jsxs as _jsxs } from "react/jsx-runtime";
import { Activity, Settings, LayoutDashboard, ShieldAlert } from 'lucide-react';
export function Sidebar({ activeTab, setActiveTab, status }) {
    const tabs = [
        { id: 'overview', icon: _jsx(LayoutDashboard, { size: 18 }), label: 'Overview' },
        { id: 'monitor', icon: _jsx(ShieldAlert, { size: 18 }), label: 'Telemetry' },
        { id: 'resources', icon: _jsx(Activity, { size: 18 }), label: 'Resources' },
        { id: 'config', icon: _jsx(Settings, { size: 18 }), label: 'Settings' }
    ];
    return (_jsxs("div", { className: "hx-sidebar", children: [_jsxs("div", { className: "hx-sidebar__logo", children: [_jsx("svg", { xmlns: "http://www.w3.org/2000/svg", viewBox: "0 0 100 100", style: { width: 28, height: 28 }, children: _jsxs("g", { transform: "translate(4, 0)", children: [_jsx("rect", { x: "15", y: "20", width: "16", height: "60", fill: "currentColor", rx: "1" }), _jsx("rect", { x: "30", y: "42", width: "14", height: "16", fill: "currentColor" }), _jsx("polygon", { points: "86,20 70,20 46,80 62,80", fill: "#3b82f6" }), _jsx("polygon", { points: "46,20 62,20 86,80 70,80", fill: "#3b82f6" }), _jsx("polygon", { points: "58,50 66,30 74,50 66,70", fill: "#2563eb" })] }) }), _jsx("span", { className: "hx-sidebar__logo-text", children: "HeuriX" })] }), _jsx("div", { className: "hx-sidebar__nav", children: tabs.map((tab) => (_jsxs("div", { className: `hx-sidebar__item ${activeTab === tab.id ? 'hx-sidebar__item--active' : ''}`, onClick: () => setActiveTab(tab.id), children: [tab.icon, _jsx("span", { children: tab.label })] }, tab.id))) }), _jsxs("div", { className: "hx-sidebar__footer", children: [_jsx("div", { className: `hx-status-dot ${status === 'running' ? 'hx-status-dot--running' : 'hx-status-dot--error'}` }), _jsx("span", { children: "Engine v0.1.0" })] })] }));
}
