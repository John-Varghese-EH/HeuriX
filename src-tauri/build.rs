fn main() -> Result<(), Box<dyn std::error::Error>> {
    // Use a vendored protoc unless the caller explicitly provides one, so the
    // build works without a (possibly broken or missing) system protobuf.
    if std::env::var_os("PROTOC").is_none() {
        std::env::set_var("PROTOC", protoc_bin_vendored::protoc_bin_path()?);
    }
    tonic_build::configure()
        .build_server(false)
        .compile(&["../proto/heurix.proto"], &["../proto"])?;
    tauri_build::build();
    Ok(())
}
