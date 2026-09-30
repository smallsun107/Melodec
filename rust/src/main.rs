//! `rencm` — decrypt NetEase Cloud Music `.ncm` files.
//!
//! ```text
//! rencm <file.ncm> [output-prefix]
//! ```

use std::fs;
use std::io::{Read, Write};
use std::process::exit;

use sha2::{Digest, Sha256};

fn main() {
    let args: Vec<String> = std::env::args().collect();
    if args.len() < 2 {
        eprintln!("usage: rencm <file.ncm> [output-prefix]");
        exit(2);
    }
    if let Err(e) = run(&args[1], args.get(2).map(String::as_str)) {
        eprintln!("error: {e}");
        exit(1);
    }
}

fn run(input: &str, out_prefix: Option<&str>) -> Result<(), Box<dyn std::error::Error>> {
    let mut dec = rencm::open(input)?;
    let stem = out_prefix
        .map(str::to_owned)
        .unwrap_or_else(|| strip_ext(input));

    let format = dec.format();
    let audio_path = format!("{stem}.{}", format.ext());

    let mut out = fs::File::create(&audio_path)?;
    let mut hasher = Sha256::new();
    let mut buf = vec![0u8; 64 * 1024];
    let mut total = 0u64;
    loop {
        let n = dec.read(&mut buf)?;
        if n == 0 {
            break;
        }
        out.write_all(&buf[..n])?;
        hasher.update(&buf[..n]);
        total += n as u64;
    }
    drop(out);

    println!("version   : {}", dec.version());
    println!("format    : {format}");
    println!("box_key   : {} bytes", dec.box_key().len());
    println!("audio     : {total} bytes");
    println!("sha256    : {}", hex(&hasher.finalize()));
    println!("output    : {audio_path}");

    if let Some(cover) = dec.cover() {
        let cover_path = format!("{stem}.jpg");
        if fs::write(&cover_path, cover).is_ok() {
            println!("cover_out : {cover_path}");
        }
    }
    // Print the metadata exactly as it appears in the file. Going through
    // `Metadata`/`serde_json::Value` would reorder object keys (BTreeMap) and
    // normalize number literals, so the raw bytes are used for display.
    if let Some(json) = dec.meta_json() {
        println!("metadata  : {}", String::from_utf8_lossy(json));
    }
    Ok(())
}

fn strip_ext(path: &str) -> String {
    match path.rfind('.') {
        Some(i) if !path[i..].contains('/') => path[..i].to_string(),
        _ => path.to_string(),
    }
}

fn hex(bytes: &[u8]) -> String {
    let mut s = String::with_capacity(bytes.len() * 2);
    for b in bytes {
        s.push_str(&format!("{b:02x}"));
    }
    s
}
