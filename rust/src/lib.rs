//! ReNcm — a streaming decoder for NetEase Cloud Music `.ncm` files.
//!
//! The format and algorithms were recovered by reverse engineering the
//! NeteaseMusic macOS binary. See docs/NCM-FORMAT-ANALYSIS.md for the full write-up and
//! the IDA addresses behind each step.
//!
//! # Streaming
//!
//! [`Decoder`] implements [`std::io::Read`], so you can pipe the decrypted
//! audio straight into a file or hasher:
//!
//! ```no_run
//! use std::io::{self, Read, Write};
//!
//! let mut dec = rencm::open("song.ncm")?;
//! let path = format!("song.{}", dec.format().ext());
//! let mut out = std::fs::File::create(path)?;
//!
//! let mut buf = [0u8; 64 * 1024];
//! loop {
//!     let n = dec.read(&mut buf)?;
//!     if n == 0 {
//!         break;
//!     }
//!     out.write_all(&buf[..n])?;
//! }
//! # Ok::<(), Box<dyn std::error::Error>>(())
//! ```
//!
//! # Pipeline
//!
//! ```text
//! key_blob --XOR 0x64--> AES-128-ECB(core key) --> PKCS#7 unpad
//!          --> strip "neteasecloudmusic"  ==> box key
//! box   = RC4-KSA(box key)
//! table = precomputed 256-byte NCM keystream, cycled over the audio
//! audio[i] ^= table[i % 256]
//! comment --XOR 0x63--> base64 --> AES-128-ECB(meta key) --> JSON
//! ```

#![forbid(unsafe_code)]

pub mod decoder;
pub mod error;
pub mod meta;

mod key;
mod rc4;

pub use decoder::{open, Decoder, Format};
pub use error::{Error, Result};
pub use meta::{Artist, Metadata};
