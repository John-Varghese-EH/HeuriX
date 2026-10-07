pub mod grpc;

use grpc::api::heurix_daemon_client::HeurixDaemonClient;
use grpc::api::telemetry_stream_response::Payload;
use grpc::api::{EngineConfig as GrpcEngineConfig, GetConfigRequest, StreamRequest};
use serde::{Deserialize, Serialize};
use std::path::PathBuf;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;
use tauri::{AppHandle, Emitter, Manager};
use tokio::sync::Mutex;
use tokio::time::{sleep, Duration};

/// Address of the HeuriX daemon. Override with HEURIX_DAEMON_ADDR.
fn daemon_addr() -> String {
    std::env::var("HEURIX_DAEMON_ADDR").unwrap_or_else(|_| "http://127.0.0.1:50051".to_string())
}

// The UI payload shapes are unchanged so the frontend needs no edits.
#[derive(Serialize, Deserialize, Debug, Clone)]
#[serde(tag = "type", content = "data")]
pub enum EngineMessage {
    #[serde(rename = "event")]
    Event {
        path: String,
        event_type: String,
        timestamp_ms: u64,
    },
    #[serde(rename = "alert")]
    Alert {
        severity: String,
        description: String,
        entropy: f64,
        pid: i32,
        process_name: String,
        action: String,
        quarantine_path: Option<String>,
        killed_pids: Option<Vec<i32>>,
        threat_score: Option<f64>,
        timestamp_ms: u64,
    },
    #[serde(rename = "stats")]
    Stats {
        cpu_percent: f64,
        mem_percent: f64,
        mem_used_mb: u64,
        mem_total_mb: u64,
        io_read_mb: f64,
        io_write_mb: f64,
    },
    #[serde(rename = "status")]
    Status { status: String },
}

impl From<Payload> for EngineMessage {
    fn from(p: Payload) -> Self {
        match p {
            Payload::Event(e) => EngineMessage::Event {
                path: e.path,
                event_type: e.event_type,
                timestamp_ms: e.timestamp_ms,
            },
            Payload::Alert(a) => EngineMessage::Alert {
                severity: a.severity,
                description: a.description,
                entropy: a.entropy,
                pid: a.pid,
                process_name: a.process_name,
                action: a.action,
                quarantine_path: (!a.quarantine_path.is_empty()).then_some(a.quarantine_path),
                killed_pids: (!a.killed_pids.is_empty()).then_some(a.killed_pids),
                threat_score: (a.threat_score > 0.0).then_some(a.threat_score),
                timestamp_ms: a.timestamp_ms,
            },
            Payload::Stats(s) => EngineMessage::Stats {
                cpu_percent: s.cpu_percent,
                mem_percent: s.mem_percent,
                mem_used_mb: s.mem_used_mb,
                mem_total_mb: s.mem_total_mb,
                io_read_mb: s.io_read_mb,
                io_write_mb: s.io_write_mb,
            },
            Payload::Status(status) => EngineMessage::Status { status },
        }
    }
}

#[derive(Serialize, Deserialize, Debug, Clone)]
pub struct EngineConfig {
    pub watch_dir: PathBuf,
    pub entropy_threshold: f64,
    pub burst_count: u32,
    pub burst_window_ms: u64,
    pub auto_kill: bool,
    pub auto_mitigate: bool,
    pub mitigation_action: String,
}

impl Default for EngineConfig {
    fn default() -> Self {
        Self {
            watch_dir: dirs::home_dir()
                .unwrap_or_else(|| PathBuf::from("."))
                .join("Documents"),
            entropy_threshold: 7.5,
            burst_count: 15,
            burst_window_ms: 2000,
            auto_kill: false,
            auto_mitigate: true,
            mitigation_action: "suspend".to_string(),
        }
    }
}

impl From<GrpcEngineConfig> for EngineConfig {
    fn from(c: GrpcEngineConfig) -> Self {
        Self {
            watch_dir: PathBuf::from(c.watch_dir),
            entropy_threshold: c.entropy_threshold,
            burst_count: c.burst_count,
            burst_window_ms: c.burst_window_ms,
            auto_kill: c.auto_kill,
            auto_mitigate: c.auto_mitigate,
            mitigation_action: c.mitigation_action,
        }
    }
}

impl From<&EngineConfig> for GrpcEngineConfig {
    fn from(c: &EngineConfig) -> Self {
        Self {
            watch_dir: c.watch_dir.to_string_lossy().to_string(),
            entropy_threshold: c.entropy_threshold,
            burst_count: c.burst_count,
            burst_window_ms: c.burst_window_ms,
            auto_kill: c.auto_kill,
            auto_mitigate: c.auto_mitigate,
            mitigation_action: c.mitigation_action.clone(),
        }
    }
}

pub struct EngineState {
    connected: Arc<AtomicBool>,
    /// Last config seen from / sent to the daemon (used when it is offline).
    config: Arc<Mutex<EngineConfig>>,
}

impl EngineState {
    pub fn new() -> Self {
        Self {
            connected: Arc::new(AtomicBool::new(false)),
            config: Arc::new(Mutex::new(EngineConfig::default())),
        }
    }
}

async fn client() -> Result<HeurixDaemonClient<tonic::transport::Channel>, String> {
    HeurixDaemonClient::connect(daemon_addr())
        .await
        .map_err(|e| format!("HeuriX daemon not reachable at {}: {}", daemon_addr(), e))
}

fn emit_status(app: &AppHandle, status: &str) {
    let _ = app.emit(
        "heurix://status",
        EngineMessage::Status { status: status.to_string() },
    );
}

/// Keeps a telemetry stream open to the daemon, reconnecting with backoff.
async fn telemetry_loop(app: AppHandle) {
    let mut backoff = Duration::from_secs(1);
    loop {
        let connected = app.state::<EngineState>().connected.clone();

        if let Ok(mut c) = client().await {
            if let Ok(resp) = c.stream_telemetry(StreamRequest {}).await {
                connected.store(true, Ordering::SeqCst);
                backoff = Duration::from_secs(1);

                // Refresh cached config from the daemon's source of truth.
                if let Ok(cfg) = c.get_config(GetConfigRequest {}).await {
                    *app.state::<EngineState>().config.lock().await = cfg.into_inner().into();
                }

                let mut stream = resp.into_inner();
                loop {
                    match stream.message().await {
                        Ok(Some(msg)) => {
                            let Some(payload) = msg.payload else { continue };
                            let ui: EngineMessage = payload.into();
                            let topic = match ui {
                                EngineMessage::Event { .. } => "heurix://fs-event",
                                EngineMessage::Alert { .. } => "heurix://alert",
                                EngineMessage::Stats { .. } => "heurix://stats",
                                EngineMessage::Status { .. } => "heurix://status",
                            };
                            let _ = app.emit(topic, &ui);
                        }
                        Ok(None) => break,
                        Err(e) => {
                            eprintln!("telemetry stream error: {e}");
                            break;
                        }
                    }
                }
            }
        }

        if connected.swap(false, Ordering::SeqCst) {
            emit_status(&app, "disconnected");
        }
        sleep(backoff).await;
        backoff = (backoff * 2).min(Duration::from_secs(10));
    }
}

#[tauri::command]
fn get_engine_status(state: tauri::State<EngineState>) -> bool {
    state.connected.load(Ordering::SeqCst)
}

/// The daemon's lifecycle is owned by the OS service manager; the UI can only
/// report whether it is reachable.
#[tauri::command]
async fn restart_engine(state: tauri::State<'_, EngineState>) -> Result<(), String> {
    if state.connected.load(Ordering::SeqCst) {
        Ok(())
    } else {
        client().await.map(|_| ()).map_err(|e| {
            format!("{e}. Start it with `sudo systemctl start heurix` (Linux) or the Services console (Windows).")
        })
    }
}

#[tauri::command]
async fn update_config(
    config_json: String,
    state: tauri::State<'_, EngineState>,
) -> Result<(), String> {
    let v: serde_json::Value = serde_json::from_str(&config_json).map_err(|e| e.to_string())?;

    // Start from the current config so fields the UI doesn't send are kept.
    let mut cfg = state.config.lock().await.clone();
    if let Some(x) = v["watch_dir"].as_str() { cfg.watch_dir = PathBuf::from(x); }
    if let Some(x) = v["entropy_threshold"].as_f64() { cfg.entropy_threshold = x; }
    if let Some(x) = v["burst_count"].as_u64() { cfg.burst_count = x as u32; }
    if let Some(x) = v["burst_window_ms"].as_u64() { cfg.burst_window_ms = x; }
    if let Some(x) = v["auto_mitigate"].as_bool() { cfg.auto_mitigate = x; }
    if let Some(x) = v["auto_kill"].as_bool() { cfg.auto_kill = x; }
    if let Some(x) = v["mitigation_action"].as_str() { cfg.mitigation_action = x.to_string(); }

    let resp = client()
        .await?
        .update_config(GrpcEngineConfig::from(&cfg))
        .await
        .map_err(|s| s.message().to_string())?
        .into_inner();

    if !resp.success {
        return Err(resp.message);
    }
    *state.config.lock().await = cfg;
    Ok(())
}

#[tauri::command]
async fn get_config(state: tauri::State<'_, EngineState>) -> Result<String, String> {
    if let Ok(mut c) = client().await {
        if let Ok(resp) = c.get_config(GetConfigRequest {}).await {
            *state.config.lock().await = resp.into_inner().into();
        }
    }
    let cfg = state.config.lock().await.clone();
    serde_json::to_string_pretty(&cfg).map_err(|e| e.to_string())
}

pub fn run() {
    tauri::Builder::default()
        .plugin(tauri_plugin_store::Builder::default().build())
        .manage(EngineState::new())
        .invoke_handler(tauri::generate_handler![
            get_engine_status,
            restart_engine,
            update_config,
            get_config
        ])
        .setup(|app| {
            tauri::async_runtime::spawn(telemetry_loop(app.handle().clone()));
            Ok(())
        })
        .run(tauri::generate_context!())
        .expect("failed to run HeuriX");
}