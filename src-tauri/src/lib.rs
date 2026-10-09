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
        #[serde(default)]
        io_read_mb: f64,
        #[serde(default)]
        io_write_mb: f64,
    },
    #[serde(rename = "status")]
    Status { status: String },
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

pub struct EngineState {
    connected: Arc<AtomicBool>,
    config: Arc<Mutex<EngineConfig>>,
    /// Tracks the child process so we can kill it on exit.
    daemon_pid: Arc<Mutex<Option<u32>>>,
    /// The resolved path to the project root (parent of src-tauri).
    project_root: PathBuf,
}

impl EngineState {
    pub fn new(project_root: PathBuf) -> Self {
        Self {
            connected: Arc::new(AtomicBool::new(false)),
            config: Arc::new(Mutex::new(EngineConfig::default())),
            daemon_pid: Arc::new(Mutex::new(None)),
            project_root,
        }
    }
}

fn emit_status(app: &AppHandle, status: &str) {
    let _ = app.emit(
        "heurix://status",
        EngineMessage::Status { status: status.to_string() },
    );
}

/// Find the engine binary by searching known paths relative to the project root.
fn find_engine_binary(project_root: &PathBuf) -> Option<PathBuf> {
    let candidates = [
        project_root.join("build/heurix-engine"),
        project_root.join("target/release/heurix-engine"),
        PathBuf::from("/usr/local/bin/heurix-engine"),
    ];
    for p in &candidates {
        if p.exists() {
            return Some(p.clone());
        }
    }
    None
}

/// Launch the daemon process with the given config. Returns the PID on success.
fn launch_daemon(bin: &PathBuf, cfg: &EngineConfig) -> Result<u32, String> {
    let mut cmd = std::process::Command::new(bin);
    cmd.arg("--watch").arg(&cfg.watch_dir);
    cmd.arg("--entropy-threshold").arg(cfg.entropy_threshold.to_string());
    cmd.arg("--burst-count").arg(cfg.burst_count.to_string());
    cmd.arg("--burst-window-ms").arg(cfg.burst_window_ms.to_string());
    if cfg.auto_kill {
        cmd.arg("--auto-kill").arg("true");
    }
    cmd.arg("--auto-mitigate").arg(if cfg.auto_mitigate { "true" } else { "false" });

    // Detach stdout/stderr so the child doesn't block on pipe buffers.
    cmd.stdout(std::process::Stdio::null());
    cmd.stderr(std::process::Stdio::inherit());

    let child = cmd.spawn().map_err(|e| format!("Failed to spawn daemon: {}", e))?;
    Ok(child.id())
}

/// Keeps a telemetry stream open to the daemon, reconnecting with backoff.
/// On first connect failure, tries to auto-launch the daemon.
async fn telemetry_loop(app: AppHandle) {
    let mut backoff = Duration::from_millis(500);
    let client = reqwest::Client::builder()
        .timeout(Duration::from_secs(300))
        .build()
        .unwrap_or_default();

    let mut auto_launch_attempted = false;

    loop {
        let connected = app.state::<EngineState>().connected.clone();

        let resp = client.get(format!("{}/telemetry", daemon_addr())).send().await;
        match resp {
            Ok(mut stream) => {
                connected.store(true, Ordering::SeqCst);
                emit_status(&app, "connected");
                backoff = Duration::from_millis(500);
                auto_launch_attempted = false;

                // Refresh cached config from the daemon.
                if let Ok(cfg_resp) = client.get(format!("{}/config", daemon_addr())).send().await {
                    if let Ok(cfg) = cfg_resp.json::<EngineConfig>().await {
                        *app.state::<EngineState>().config.lock().await = cfg;
                    }
                }

                let mut buffer = Vec::new();
                while let Some(chunk_res) = stream.chunk().await.unwrap_or(None) {
                    buffer.extend_from_slice(&chunk_res);

                    while let Some(idx) = buffer.iter().position(|&b| b == b'\n') {
                        let line_bytes = buffer[..idx].to_vec();
                        buffer = buffer[idx + 1..].to_vec();

                        let line = String::from_utf8_lossy(&line_bytes);
                        if line.trim().is_empty() {
                            continue;
                        }

                        match serde_json::from_str::<EngineMessage>(&line) {
                            Ok(ui) => {
                                let topic = match &ui {
                                    EngineMessage::Event { .. } => "heurix://fs-event",
                                    EngineMessage::Alert { .. } => "heurix://alert",
                                    EngineMessage::Stats { .. } => "heurix://stats",
                                    EngineMessage::Status { .. } => "heurix://status",
                                };
                                let _ = app.emit(topic, &ui);
                            }
                            Err(e) => {
                                eprintln!("[HeuriX] JSON parse error: {} on line: {}", e, line);
                            }
                        }
                    }
                }
            }
            Err(_) => {
                // Daemon not reachable — try to auto-launch once.
                if !auto_launch_attempted {
                    auto_launch_attempted = true;
                    let state = app.state::<EngineState>();
                    if let Some(bin) = find_engine_binary(&state.project_root) {
                        let cfg = state.config.lock().await.clone();
                        match launch_daemon(&bin, &cfg) {
                            Ok(pid) => {
                                eprintln!("[HeuriX] Auto-launched daemon (PID {}) from {:?}", pid, bin);
                                *state.daemon_pid.lock().await = Some(pid);
                                // Give daemon time to bind its port.
                                sleep(Duration::from_millis(800)).await;
                                continue; // Retry connection immediately.
                            }
                            Err(e) => eprintln!("[HeuriX] Failed to auto-launch daemon: {}", e),
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

#[tauri::command]
async fn restart_engine(state: tauri::State<'_, EngineState>) -> Result<(), String> {
    if state.connected.load(Ordering::SeqCst) {
        return Ok(());
    }

    // Check if daemon is already reachable.
    let client = reqwest::Client::new();
    if client.get(format!("{}/config", daemon_addr())).send().await.is_ok() {
        return Ok(());
    }

    // Kill any stale daemon we previously launched.
    if let Some(pid) = state.daemon_pid.lock().await.take() {
        let _ = std::process::Command::new("kill").arg(pid.to_string()).status();
        sleep(Duration::from_millis(300)).await;
    }

    let bin = find_engine_binary(&state.project_root)
        .ok_or_else(|| "Cannot find heurix-engine binary. Run `cmake --build build` first.".to_string())?;

    let cfg = state.config.lock().await.clone();
    let pid = launch_daemon(&bin, &cfg)?;
    *state.daemon_pid.lock().await = Some(pid);

    // Wait for daemon to become reachable.
    for _ in 0..10 {
        sleep(Duration::from_millis(300)).await;
        if client.get(format!("{}/config", daemon_addr())).send().await.is_ok() {
            return Ok(());
        }
    }

    Err("Daemon launched but not reachable after 3s. Check build/heurix-engine output.".to_string())
}

#[tauri::command]
async fn update_config(
    config_json: String,
    state: tauri::State<'_, EngineState>,
) -> Result<(), String> {
    let v: serde_json::Value = serde_json::from_str(&config_json).map_err(|e| e.to_string())?;

    let mut cfg = state.config.lock().await.clone();
    if let Some(x) = v["watch_dir"].as_str() { cfg.watch_dir = PathBuf::from(x); }
    if let Some(x) = v["entropy_threshold"].as_f64() { cfg.entropy_threshold = x; }
    if let Some(x) = v["burst_count"].as_u64() { cfg.burst_count = x as u32; }
    if let Some(x) = v["burst_window_ms"].as_u64() { cfg.burst_window_ms = x; }
    if let Some(x) = v["auto_mitigate"].as_bool() { cfg.auto_mitigate = x; }
    if let Some(x) = v["auto_kill"].as_bool() { cfg.auto_kill = x; }
    if let Some(x) = v["mitigation_action"].as_str() { cfg.mitigation_action = x.to_string(); }

    let client = reqwest::Client::new();
    let res = client
        .post(format!("{}/config", daemon_addr()))
        .json(&cfg)
        .send()
        .await
        .map_err(|e| e.to_string())?;

    if !res.status().is_success() {
        return Err(format!("Backend rejected config update: {}", res.status()));
    }
    *state.config.lock().await = cfg;
    Ok(())
}

#[tauri::command]
async fn get_config(state: tauri::State<'_, EngineState>) -> Result<String, String> {
    let client = reqwest::Client::new();
    if let Ok(resp) = client.get(format!("{}/config", daemon_addr())).send().await {
        if let Ok(cfg) = resp.json::<EngineConfig>().await {
            *state.config.lock().await = cfg;
        }
    }
    let cfg = state.config.lock().await.clone();
    serde_json::to_string_pretty(&cfg).map_err(|e| e.to_string())
}

pub fn run() {
    tauri::Builder::default()
        .plugin(tauri_plugin_store::Builder::default().build())
        .invoke_handler(tauri::generate_handler![
            get_engine_status,
            restart_engine,
            update_config,
            get_config
        ])
        .setup(|app| {
            // Resolve project root: src-tauri/../ = project root.
            let project_root = app.path().resource_dir()
                .unwrap_or_else(|_| std::env::current_dir().unwrap_or_default());

            // In dev mode, current_dir is the project root.
            // In production, resource_dir points inside the bundle.
            // We also check CARGO_MANIFEST_DIR for dev builds.
            let root = if let Ok(manifest_dir) = std::env::var("CARGO_MANIFEST_DIR") {
                PathBuf::from(manifest_dir).parent().map(|p| p.to_path_buf()).unwrap_or(project_root)
            } else {
                project_root
            };

            eprintln!("[HeuriX] Project root: {:?}", root);

            app.manage(EngineState::new(root));
            tauri::async_runtime::spawn(telemetry_loop(app.handle().clone()));
            Ok(())
        })
        .run(tauri::generate_context!())
        .expect("failed to run HeuriX");
}