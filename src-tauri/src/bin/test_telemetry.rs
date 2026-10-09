use reqwest;
use serde_json::Value;

#[tokio::main]
async fn main() {
    let mut retry_count = 0;
    loop {
        match reqwest::get("http://127.0.0.1:50051/telemetry").await {
            Ok(mut resp) => {
                println!("Connected to telemetry stream");
                let mut buffer = String::new();
                while let Some(chunk_res) = resp.chunk().await.unwrap_or(None) {
                    let chunk_str = String::from_utf8_lossy(&chunk_res);
                    buffer.push_str(&chunk_str);
                    while let Some(idx) = buffer.find('\n') {
                        let line = buffer[..idx].to_string();
                        buffer = buffer[idx + 1..].to_string();
                        if line.trim().is_empty() { continue; }
                        println!("LINE: {}", line);
                    }
                }
                println!("Stream ended.");
            },
            Err(e) => {
                println!("Error connecting: {}", e);
                tokio::time::sleep(tokio::time::Duration::from_secs(1)).await;
            }
        }
    }
}
