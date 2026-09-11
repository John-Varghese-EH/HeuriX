use serde::{Deserialize, Serialize};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;
use std::path::PathBuf;
use tokio::sync::Mutex;
use tokio::time::{sleep, Duration};
use tauri::{AppHandle, Manager, Emitter};
use tauri_plugin_shell::ShellExt;
use tauri_plugin_shell::process::CommandChild;

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
        #[serde(default)]
        entropy: f64,
        #[serde(default)]
        pid: i32,
        #[serde(default)]
        process_name: String,
        #[serde(default)]
        action: String,
        #[serde(default)]
        quarantine_path: Option<String>,
        #[serde(default)]
        killed_pids: Option<Vec<i32>>,
        #[serde(default)]
        threat_score: Option<f64>,
        timestamp_ms: u64,
    },
    #[serde(rename = "stats")]
    Stats {
        #[serde(default)]
        cpu_percent: f64,
        #[serde(default)]
        mem_percent: f64,
        #[serde(default)]
        mem_used_mb: u64,
        #[serde(default)]
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
    child: Arc<Mutex<Option<CommandChild>>>,
    running: Arc<AtomicBool>,
    config: Arc<Mutex<EngineConfig>>,
    restart_count: Arc<std::sync::atomic::AtomicU32>,
    last_heartbeat: Arc<Mutex<Option<std::time::Instant>>>,
}

impl EngineState {
    pub fn new() -> Self {
        Self {
            child: Arc::new(Mutex::new(None)),
            running: Arc::new(AtomicBool::new(false)),
            config: Arc::new(Mutex::new(EngineConfig::default())),
            restart_count: Arc::new(std::sync::atomic::AtomicU32::new(0)),
            last_heartbeat: Arc::new(Mutex::new(None)),
        }
    }

    fn config_path(&self) -> PathBuf {
        dirs::config_dir()
            .unwrap_or_else(|| PathBuf::from("."))
            .join("heurix")
            .join("config.json")
    }

    pub async fn load_config(&self) -> Result<(), String> {
        let path = self.config_path();
        if path.exists() {
            let data = tokio::fs::read_to_string(&path)
                .await
                .map_err(|e| e.to_string())?;
            let config: EngineConfig = serde_json::from_str(&data)
                .map_err(|e| format!("Failed to parse config: {}", e))?;
            *self.config.lock().await = config;
        }
        Ok(())
    }

    pub async fn save_config(&self) -> Result<(), String> {
        let config = self.config.lock().await.clone();
        let path = self.config_path();
        if let Some(parent) = path.parent() {
            tokio::fs::create_dir_all(parent)
                .await
                .map_err(|e| e.to_string())?;
        }
        let data = serde_json::to_string_pretty(&config)
            .map_err(|e| e.to_string())?;
        tokio::fs::write(&path, data)
            .await
            .map_err(|e| e.to_string())?;
        Ok(())
    }

    pub fn check_health(&self) -> bool {
        self.running.load(Ordering::SeqCst)
    }

    pub fn reset_restart_count(&self) {
        self.restart_count.store(0, Ordering::SeqCst);
    }
}

pub async fn spawn_engine(app: &AppHandle, config: Option<EngineConfig>) -> Result<(), String> {
    let state = app.state::<EngineState>();

    // If config provided, update and save
    if let Some(new_config) = config {
        *state.config.lock().await = new_config;
        state.save_config().await?;
    }

    // Check restart limit
    let restart_count = state.restart_count.fetch_add(1, Ordering::SeqCst);
    if restart_count >= 5 {
        return Err("Engine restart limit reached. Please restart the application.".to_string());
    }

    // Stop existing engine if running
    if state.running.load(Ordering::SeqCst) {
        let mut child_guard = state.child.lock().await;
        if let Some(child) = child_guard.take() {
            let _ = child.kill();
        }
        state.running.store(false, Ordering::SeqCst);
    }

    // Build engine arguments from config
    let config = state.config.lock().await.clone();
    let args = vec![
        "--watch".to_string(),
        config.watch_dir.to_string_lossy().to_string(),
        "--auto-kill".to_string(),
        config.auto_kill.to_string(),
        "--entropy-threshold".to_string(),
        config.entropy_threshold.to_string(),
        "--burst-count".to_string(),
        config.burst_count.to_string(),
        "--burst-window-ms".to_string(),
        config.burst_window_ms.to_string(),
    ];

    // Spawn engine
    let (mut rx, child) = app
        .shell()
        .sidecar("heurix-engine")
        .map_err(|e| e.to_string())?
        .args(&args)
        .spawn()
        .map_err(|e| e.to_string())?;

    let child_arc = Arc::clone(&state.child);
    let running_arc = Arc::clone(&state.running);
    let last_heartbeat_arc = Arc::clone(&state.last_heartbeat);
    let app_clone = app.clone();

    running_arc.store(true, Ordering::SeqCst);

    // Spawn message handler
    tauri::async_runtime::spawn(async move {
        *child_arc.lock().await = Some(child);

        while let Some(event) = rx.recv().await {
            match event {
                tauri_plugin_shell::process::CommandEvent::Stdout(line) => {
                    if let Ok(line_str) = String::from_utf8(line) {
                        for l in line_str.lines() {
                            if l.trim().is_empty() {
                                continue;
                            }
                            if let Ok(msg) = serde_json::from_str::<EngineMessage>(l) {
                                match msg {
                                    EngineMessage::Event { .. } => {
                                        let _ = app_clone.emit("heurix://fs-event", &msg);
                                    }
                                    EngineMessage::Alert { .. } => {
                                        let _ = app_clone.emit("heurix://alert", &msg);
                                    }
                                    EngineMessage::Stats { .. } => {
                                        let _ = app_clone.emit("heurix://stats", &msg);
                                    }
                                    EngineMessage::Status { ref status, .. } => {
                                        if status == "heartbeat" {
                                            *last_heartbeat_arc.lock().await = Some(std::time::Instant::now());
                                        }
                                        let _ = app_clone.emit("heurix://status", &msg);
                                    }
                                }
                            }
                        }
                    }
                }
                tauri_plugin_shell::process::CommandEvent::Stderr(line) => {
                    if let Ok(line_str) = String::from_utf8(line) {
                        eprintln!("Engine stderr: {}", line_str);
                    }
                }
                tauri_plugin_shell::process::CommandEvent::Error(err) => {
                    eprintln!("Engine error: {}", err);
                }
                tauri_plugin_shell::process::CommandEvent::Terminated(payload) => {
                    println!("Engine terminated with payload: {:?}", payload);
                    running_arc.store(false, Ordering::SeqCst);
                    *child_arc.lock().await = None;
                    break;
                }
                _ => {}
            }
        }
    });

    // Spawn health monitor - emits events but does NOT auto-restart
    // (auto-restart from within an async task causes Send issues)
    // Instead, it notifies the frontend so user can trigger restart.
    let app_clone = app.clone();
    tauri::async_runtime::spawn(async move {
        loop {
            sleep(Duration::from_secs(10)).await;

            let state = app_clone.state::<EngineState>();
            let is_running = state.running.load(Ordering::SeqCst);

            if is_running {
                let last_heartbeat = *state.last_heartbeat.lock().await;
                if let Some(last) = last_heartbeat {
                    if last.elapsed() > Duration::from_secs(30) {
                        eprintln!("Engine heartbeat timeout - engine may be hung");
                        state.running.store(false, Ordering::SeqCst);
                        let _ = app_clone.emit("heurix://status", EngineMessage::Status {
                            status: "heartbeat_timeout".to_string()
                        });
                    }
                }
            }
        }
    });

    // Reset restart count after successful start
    state.reset_restart_count();

    Ok(())
}

#[tauri::command]
fn get_engine_status(state: tauri::State<EngineState>) -> bool {
    state.running.load(Ordering::SeqCst)
}

#[tauri::command]
async fn send_engine_command(command: String, state: tauri::State<'_, EngineState>) -> Result<(), String> {
    let mut child_guard = state.child.lock().await;
    if let Some(child) = child_guard.as_mut() {
        let cmd = format!("{}\n", command);
        child.write(cmd.as_bytes()).map_err(|e| e.to_string())?;
        Ok(())
    } else {
        Err("Engine not running".to_string())
    }
}

#[tauri::command]
async fn start_engine(app: AppHandle) -> Result<(), String> {
    spawn_engine(&app, None).await
}

#[tauri::command]
async fn stop_engine(state: tauri::State<'_, EngineState>) -> Result<(), String> {
    let mut child_guard = state.child.lock().await;
    if let Some(mut child) = child_guard.take() {
        let _ = child.write(b"{\"command\":\"shutdown\"}\n");
        let _ = child.kill();
        state.running.store(false, Ordering::SeqCst);
        Ok(())
    } else {
        Err("Engine not running".to_string())
    }
}

#[tauri::command]
async fn update_config(app: AppHandle, config_json: String) -> Result<(), String> {
    let v: serde_json::Value = serde_json::from_str(&config_json).map_err(|e| e.to_string())?;

    let mut config = EngineConfig::default();
    if let Some(watch_dir) = v["watch_dir"].as_str() {
        config.watch_dir = PathBuf::from(watch_dir);
    }
    if let Some(threshold) = v["entropy_threshold"].as_f64() {
        config.entropy_threshold = threshold;
    }
    if let Some(count) = v["burst_count"].as_u64() {
        config.burst_count = count as u32;
    }
    if let Some(window) = v["burst_window_ms"].as_u64() {
        config.burst_window_ms = window;
    }
    if let Some(auto_kill) = v["auto_mitigate"].as_bool() {
        config.auto_kill = auto_kill;
    }
    if let Some(action) = v["mitigation_action"].as_str() {
        config.mitigation_action = action.to_string();
    }

    spawn_engine(&app, Some(config)).await
}

#[tauri::command]
async fn get_config(state: tauri::State<'_, EngineState>) -> Result<String, String> {
    let config = state.config.lock().await.clone();
    serde_json::to_string_pretty(&config).map_err(|e| e.to_string())
}

#[tauri::command]
async fn restart_engine(app: AppHandle) -> Result<(), String> {
    spawn_engine(&app, None).await
}

async fn init_engine(app_handle: AppHandle) {
    let state = app_handle.state::<EngineState>();
    let inner = state.inner();
    let _ = inner.load_config().await;
    let _ = spawn_engine(&app_handle, None).await;
}

pub fn run() {
    tauri::Builder::default()
        .plugin(tauri_plugin_shell::init())
        .plugin(tauri_plugin_store::Builder::default().build())
        .manage(EngineState::new())
        .invoke_handler(tauri::generate_handler![
            get_engine_status,
            send_engine_command,
            start_engine,
            stop_engine,
            update_config,
            get_config,
            restart_engine
        ])
        .setup(|app| {
            let app_handle = app.handle().clone();
            tauri::async_runtime::spawn(init_engine(app_handle));
            Ok(())
        })
        .run(tauri::generate_context!())
        .expect("failed to run HeuriX");
}