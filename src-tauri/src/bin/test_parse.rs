use serde::{Deserialize, Serialize};

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

fn main() {
    let json = r#"{"type":"event","data":{"path":"./canary/test_both.txt","event_type":"create","timestamp_ms":1791523843086}}"#;
    match serde_json::from_str::<EngineMessage>(json) {
        Ok(msg) => println!("Parsed: {:?}", msg),
        Err(e) => println!("Error: {}", e),
    }
    let event = EngineMessage::Event {
        path: "a".to_string(),
        event_type: "b".to_string(),
        timestamp_ms: 123,
    };
    println!("Serialized event: {}", serde_json::to_string(&event).unwrap());
}
