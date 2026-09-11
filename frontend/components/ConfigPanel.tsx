import { useState, useEffect } from 'react';
import { invoke } from '@tauri-apps/api/core';

interface ConfigPanelProps {
  theme: string;
  setTheme: (theme: any) => void;
  watchDir: string;
  setWatchDir: (dir: string) => void;
  onRestartEngine: () => void;
  isRestarting: boolean;
  engineStatus: string;
}

export function ConfigPanel({ theme, setTheme, watchDir, setWatchDir, onRestartEngine, isRestarting, engineStatus }: ConfigPanelProps) {
  const [entropy, setEntropy] = useState(7.5);
  const [burstCount, setBurstCount] = useState(15);
  const [burstWindow, setBurstWindow] = useState(2000);
  const [mitigation, setMitigation] = useState('suspend');
  const [autoMitigate, setAutoMitigate] = useState(true);

  const [statusMsg, setStatusMsg] = useState<{type: 'success'|'error', text: string} | null>(null);

  // Load config on mount
  useEffect(() => {
    const loadConfig = async () => {
      try {
        const configStr = await invoke<string>('get_config');
        const config = JSON.parse(configStr);
        if (config.watch_dir) setWatchDir(config.watch_dir);
        if (config.entropy_threshold) setEntropy(config.entropy_threshold);
        if (config.burst_count) setBurstCount(config.burst_count);
        if (config.burst_window_ms) setBurstWindow(config.burst_window_ms);
        if (config.mitigation_action) setMitigation(config.mitigation_action);
        if (config.auto_mitigate !== undefined) setAutoMitigate(config.auto_mitigate);
      } catch (e) {
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
    } catch (e) {
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

  return (
    <div className="hx-page-container">
      <div className="hx-header-row">
        <div>
          <h1 className="hx-page-title">Settings</h1>
          <p className="hx-page-desc">Configure detection thresholds and mitigation strategies.</p>
        </div>
        <div style={{ display: 'flex', gap: '12px' }}>
          <button className="hx-btn" onClick={handleResetDefaults}>Reset Defaults</button>
          <button className="hx-btn hx-btn--primary" onClick={handleApply}>Save Changes</button>
        </div>
      </div>

      {statusMsg && (
        <div style={{
          padding: '12px 16px',
          marginBottom: '24px',
          borderRadius: '6px',
          fontSize: '13px',
          background: statusMsg.type === 'error' ? 'rgba(229, 72, 77, 0.1)' : 'rgba(46, 139, 87, 0.1)',
          color: statusMsg.type === 'error' ? 'var(--hx-accent-red)' : 'var(--hx-accent-green)',
          border: `1px solid ${statusMsg.type === 'error' ? 'rgba(229, 72, 77, 0.2)' : 'rgba(46, 139, 87, 0.2)'}`
        }}>
          {statusMsg.text}
        </div>
      )}

      <div className="panel" style={{ padding: '32px', marginBottom: '24px' }}>
        <h2 style={{ fontSize: '16px', fontWeight: 600, marginBottom: '24px' }}>Theme Appearance</h2>

        <div className="hx-form-group">
          <div className="hx-radio-group" style={{ gridTemplateColumns: 'repeat(3, 1fr)' }}>
            <div className="hx-radio-card" data-active={theme === 'system'} onClick={() => setTheme('system')}>
              <div className="hx-radio-title">System</div>
              <div className="hx-radio-desc">Match OS preference</div>
            </div>
            <div className="hx-radio-card" data-active={theme === 'dark'} onClick={() => setTheme('dark')}>
              <div className="hx-radio-title">Dark Mode</div>
              <div className="hx-radio-desc">Premium stealth aesthetic</div>
            </div>
            <div className="hx-radio-card" data-active={theme === 'light'} onClick={() => setTheme('light')}>
              <div className="hx-radio-title">Light Mode</div>
              <div className="hx-radio-desc">Clean, bright workspace</div>
            </div>
          </div>
        </div>
      </div>

      <div className="panel" style={{ padding: '32px', marginBottom: '24px' }}>
        <h2 style={{ fontSize: '16px', fontWeight: 600, marginBottom: '24px' }}>Monitoring Configuration</h2>

        <div className="hx-form-group">
          <label className="hx-form-label">Watch Directory</label>
          <span className="hx-form-desc">The absolute path to the directory that HeuriX will monitor recursively.</span>
          <input type="text" className="hx-input" value={watchDir} onChange={(e) => setWatchDir(e.target.value)} />
        </div>
      </div>

      <div className="panel" style={{ padding: '32px', marginBottom: '24px' }}>
        <h2 style={{ fontSize: '16px', fontWeight: 600, marginBottom: '24px' }}>Detection Thresholds</h2>

        <div className="hx-form-group">
          <label className="hx-form-label">Entropy Threshold ({entropy.toFixed(1)})</label>
          <span className="hx-form-desc">Shannon entropy threshold (0-8). Highly encrypted files approach 8.0.</span>
          <input type="range" min="0" max="8" step="0.1" className="hx-slider" value={entropy} onChange={(e) => setEntropy(parseFloat(e.target.value))} />
        </div>

        <div style={{ display: 'flex', gap: '24px' }}>
          <div className="hx-form-group" style={{ flex: 1 }}>
            <label className="hx-form-label">Burst Count</label>
            <span className="hx-form-desc">Number of file mutations required to trigger a burst alert.</span>
            <input type="number" min="1" max="1000" className="hx-input" value={burstCount} onChange={(e) => setBurstCount(parseInt(e.target.value))} />
          </div>
          <div className="hx-form-group" style={{ flex: 1 }}>
            <label className="hx-form-label">Burst Window (ms)</label>
            <span className="hx-form-desc">Time window in milliseconds for burst detection.</span>
            <input type="number" min="100" max="10000" className="hx-input" value={burstWindow} onChange={(e) => setBurstWindow(parseInt(e.target.value))} />
          </div>
        </div>
      </div>

      <div className="panel" style={{ padding: '32px', marginBottom: '24px' }}>
        <h2 style={{ fontSize: '16px', fontWeight: 600, marginBottom: '24px' }}>Mitigation Action</h2>

        <div className="hx-form-group">
          <div className="hx-radio-group" style={{ gridTemplateColumns: 'repeat(2, 1fr)' }}>
            <div className="hx-radio-card" data-active={mitigation === 'suspend'} onClick={() => setMitigation('suspend')}>
              <div className="hx-radio-title">Suspend Process</div>
              <div className="hx-radio-desc">SIGSTOP — pause offending process for manual review.</div>
            </div>
            <div className="hx-radio-card" data-active={mitigation === 'terminate'} onClick={() => setMitigation('terminate')}>
              <div className="hx-radio-title">Terminate Process</div>
              <div className="hx-radio-desc">SIGTERM → SIGKILL — destroy the offending process.</div>
            </div>
            <div className="hx-radio-card" data-active={mitigation === 'quarantine'} onClick={() => setMitigation('quarantine')}>
              <div className="hx-radio-title">Quarantine File</div>
              <div className="hx-radio-desc">Move suspect file to .heurix-quarantine (0400) + suspend PID.</div>
            </div>
            <div className="hx-radio-card" data-active={mitigation === 'isolate'} onClick={() => setMitigation('isolate')}>
              <div className="hx-radio-title">Isolate (Tree Kill + Quarantine)</div>
              <div className="hx-radio-desc">Kill entire process tree + quarantine file. For critical.</div>
            </div>
          </div>
        </div>

        <div className="hx-toggle-wrap" style={{ cursor: 'pointer' }} onClick={() => setAutoMitigate(!autoMitigate)}>
          <div className="hx-toggle-info">
            <strong>Automatic Mitigation</strong>
            <span>Automatically execute the selected action when a critical threat is detected.</span>
          </div>
          <div style={{
            width: '36px', height: '20px', borderRadius: '10px',
            background: autoMitigate ? 'var(--hx-text-primary)' : 'var(--hx-border-hover)',
            position: 'relative', transition: 'all 0.2s'
          }}>
            <div style={{
              width: '16px', height: '16px', borderRadius: '50%', background: autoMitigate ? '#000' : 'var(--hx-text-secondary)',
              position: 'absolute', top: '2px', left: autoMitigate ? '18px' : '2px', transition: 'all 0.2s'
            }} />
          </div>
        </div>
      </div>

      <div className="panel" style={{ padding: '32px' }}>
        <h2 style={{ fontSize: '16px', fontWeight: 600, marginBottom: '24px' }}>Engine Management</h2>

        <div className="hx-form-group">
          <label className="hx-form-label">Engine Status</label>
          <div style={{ display: 'flex', alignItems: 'center', gap: '12px', padding: '16px', background: 'var(--hx-overlay-2)', borderRadius: '8px', border: '1px solid var(--hx-border)' }}>
            <div style={{
              width: '12px', height: '12px', borderRadius: '50%',
              background: engineStatus === 'running' ? 'var(--hx-accent-green)' : 'var(--hx-accent-red)',
              boxShadow: engineStatus === 'running' ? '0 0 8px var(--hx-accent-green)' : 'none'
            }} />
            <div style={{ flex: 1 }}>
              <div style={{ fontSize: '14px', fontWeight: 600, color: 'var(--hx-text-primary)' }}>
                {engineStatus === 'running' ? 'Engine Online' : 'Engine Offline'}
              </div>
              <div style={{ fontSize: '12px', color: 'var(--hx-text-secondary)', marginTop: '4px' }}>
                {engineStatus === 'running' ? 'Monitoring filesystem for threats' : 'Not monitoring - click restart to recover'}
              </div>
            </div>
            <button
              className={`hx-btn ${engineStatus === 'running' ? 'hx-btn--danger' : 'hx-btn--primary'}`}
              onClick={onRestartEngine}
              disabled={isRestarting}
              style={{ minWidth: '100px' }}
            >
              {isRestarting ? 'Restarting...' : (engineStatus === 'running' ? 'Restart' : 'Start')}
            </button>
          </div>
        </div>
      </div>
    </div>
  );
}