import { Activity, Settings, LayoutDashboard, ShieldAlert } from 'lucide-react';

interface SidebarProps {
  activeTab: string;
  setActiveTab: (tab: string) => void;
  status: string;
}

export function Sidebar({ activeTab, setActiveTab, status }: SidebarProps) {
  const tabs = [
    { id: 'overview', icon: <LayoutDashboard size={18} />, label: 'Overview' },
    { id: 'monitor', icon: <ShieldAlert size={18} />, label: 'Telemetry' },
    { id: 'resources', icon: <Activity size={18} />, label: 'Resources' },
    { id: 'config', icon: <Settings size={18} />, label: 'Settings' }
  ];

  return (
    <div className="hx-sidebar">
      <div className="hx-sidebar__logo">
        <svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 100" style={{ width: 28, height: 28 }}>
          <g transform="translate(4, 0)">
            <rect x="15" y="20" width="16" height="60" fill="currentColor" rx="1" />
            <rect x="30" y="42" width="14" height="16" fill="currentColor" />
            <polygon points="86,20 70,20 46,80 62,80" fill="#3b82f6" />
            <polygon points="46,20 62,20 86,80 70,80" fill="#3b82f6" />
            <polygon points="58,50 66,30 74,50 66,70" fill="#2563eb" />
          </g>
        </svg>
        <span className="hx-sidebar__logo-text">HeuriX</span>
      </div>
      
      <div className="hx-sidebar__nav">
        {tabs.map((tab) => (
          <div 
            key={tab.id}
            className={`hx-sidebar__item ${activeTab === tab.id ? 'hx-sidebar__item--active' : ''}`}
            onClick={() => setActiveTab(tab.id)}
          >
            {tab.icon}
            <span>{tab.label}</span>
          </div>
        ))}
      </div>

      <div className="hx-sidebar__footer">
        <div className={`hx-status-dot ${status === 'running' ? 'hx-status-dot--running' : 'hx-status-dot--error'}`} />
        <span>Engine v0.1.0</span>
      </div>
    </div>
  );
}
