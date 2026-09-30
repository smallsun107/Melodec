use std::fs::File;
use std::io::{self, BufReader, Read};
use std::path::Path;

use crate::error::{Error, Result};
use crate::key::{decrypt_comment, decrypt_key};
use crate::meta::Metadata;
use crate::rc4;

/// Decoded audio container format.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Format {
    Mp3,
    Flac,
    M4a,
    Ogg,
    Aac,
    Unknown,
}

impl Format {
    /// Usual file extension, without a leading dot.
    pub fn ext(self) -> &'static str {
        match self {
            Format::Mp3 => "mp3",
            Format::Flac => "flac",
            Format::M4a => "m4a",
            Format::Ogg => "ogg",
            Format::Aac => "aac",
            Format::Unknown => "bin",
        }
    }
}

impl std::fmt::Display for Format {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_str(self.ext())
    }
}

/// Streaming NCM decoder. Implements [`Read`] for the decrypted audio.
pub struct Decoder<R: Read> {
    inner: R,
    version: u8,
    format: Format,
    cover: Option<Vec<u8>>,
    box_key: Vec<u8>,
    meta_json: Option<Vec<u8>>,
    table: [u8; 256],
    pos: usize,
    prefix: Vec<u8>,
}

impl<R: Read> Decoder<R> {
    /// Parse the container header from `reader` and prepare streaming
    /// decryption of the audio section.
    pub fn new(reader: R) -> Result<Self> {
        let mut r = reader;

        let mut hdr = [0u8; 10];
        r.read_exact(&mut hdr)?;
        if &hdr[..8] != b"CTENFDAM" {
            return Err(Error::BadMagic);
        }
        let version = hdr[8];

        let key_blob = read_frame(&mut r)?;
        let box_key = decrypt_key(&key_blob)?;

        let comment = read_frame(&mut r)?;
        // Metadata is optional: the audio does not depend on it, so a malformed
        // comment section must not reject an otherwise valid file.
        let meta_json = if comment.is_empty() {
            None
        } else {
            decrypt_comment(&comment).ok()
        };

        skip(&mut r, 5)?;
        let cover_size = read_len(&mut r)?;
        // The container repeats the cover size; the first field is authoritative
        // (it is where the audio starts). A larger duplicate would misalign the
        // audio, so reject it rather than emit silently wrong bytes.
        let (cover_size_dup, image) = read_frame_with_len(&mut r)?;
        if cover_size < cover_size_dup {
            return Err(Error::BadCover);
        }
        if cover_size > cover_size_dup {
            skip(&mut r, (cover_size - cover_size_dup) as usize)?;
        }
        let cover = if cover_size_dup > 0 {
            Some(image)
        } else {
            None
        };

        let table = rc4::keystream(&rc4::key_box(&box_key));

        // Read the first 12 ciphertext bytes to sniff the format, then serve
        // them decrypted before streaming the rest.
        let mut buf = [0u8; 12];
        let n = read_up_to(&mut r, &mut buf)?;
        let mut prefix = buf[..n].to_vec();
        for (i, b) in prefix.iter_mut().enumerate() {
            *b ^= table[i & 0xFF];
        }
        let format = detect_format(&prefix);

        Ok(Decoder {
            inner: r,
            version,
            format,
            cover,
            box_key,
            meta_json,
            table,
            pos: n,
            prefix,
        })
    }

    /// Container version byte.
    pub fn version(&self) -> u8 {
        self.version
    }

    /// Detected audio format.
    pub fn format(&self) -> Format {
        self.format
    }

    /// Derived box key.
    pub fn box_key(&self) -> &[u8] {
        &self.box_key
    }

    /// Embedded cover image, if any.
    pub fn cover(&self) -> Option<&[u8]> {
        self.cover.as_deref()
    }

    /// Raw metadata JSON (without the `music:` prefix), if any.
    pub fn meta_json(&self) -> Option<&[u8]> {
        self.meta_json.as_deref()
    }

    /// Parsed metadata, if the file has a metadata section.
    pub fn metadata(&self) -> Result<Option<Metadata>> {
        match &self.meta_json {
            Some(json) => Ok(Some(Metadata::parse(json)?)),
            None => Ok(None),
        }
    }

    /// Copy the decrypted audio into `w`.
    pub fn write_to<W: io::Write>(&mut self, w: &mut W) -> io::Result<u64> {
        let mut buf = [0u8; 64 * 1024];
        let mut total = 0u64;
        loop {
            let n = self.read(&mut buf)?;
            if n == 0 {
                return Ok(total);
            }
            w.write_all(&buf[..n])?;
            total += n as u64;
        }
    }
}

impl<R: Read> Read for Decoder<R> {
    fn read(&mut self, buf: &mut [u8]) -> io::Result<usize> {
        if !self.prefix.is_empty() {
            let n = buf.len().min(self.prefix.len());
            buf[..n].copy_from_slice(&self.prefix[..n]);
            self.prefix.drain(..n);
            return Ok(n);
        }
        let n = self.inner.read(buf)?;
        for b in buf[..n].iter_mut() {
            *b ^= self.table[self.pos & 0xFF];
            self.pos += 1;
        }
        Ok(n)
    }
}

impl<R: Read> std::fmt::Debug for Decoder<R> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("Decoder")
            .field("version", &self.version)
            .field("format", &self.format)
            .field("box_key_len", &self.box_key.len())
            .field("has_cover", &self.cover.is_some())
            .finish_non_exhaustive()
    }
}

/// Open an NCM file from disk.
pub fn open<P: AsRef<Path>>(path: P) -> Result<Decoder<BufReader<File>>> {
    let f = File::open(path)?;
    Decoder::new(BufReader::new(f))
}

fn read_len<R: Read>(r: &mut R) -> Result<u32> {
    let mut b = [0u8; 4];
    r.read_exact(&mut b)?;
    Ok(u32::from_le_bytes(b))
}

/// Upper bound on a single container section (key / comment / cover), so a
/// crafted length field cannot drive a huge allocation.
const MAX_SECTION_SIZE: u32 = 64 << 20;

fn read_frame_with_len<R: Read>(r: &mut R) -> Result<(u32, Vec<u8>)> {
    let len = read_len(r)?;
    if len == 0 {
        return Ok((0, Vec::new()));
    }
    if len > MAX_SECTION_SIZE {
        return Err(Error::SectionTooLarge);
    }
    let mut data = vec![0u8; len as usize];
    r.read_exact(&mut data)?;
    Ok((len, data))
}

fn read_frame<R: Read>(r: &mut R) -> Result<Vec<u8>> {
    Ok(read_frame_with_len(r)?.1)
}

fn skip<R: Read>(r: &mut R, n: usize) -> Result<()> {
    if n == 0 {
        return Ok(());
    }
    // Stream the bytes away rather than allocating a buffer just to discard them.
    let copied = io::copy(&mut r.by_ref().take(n as u64), &mut io::sink())?;
    if copied < n as u64 {
        return Err(Error::Io(io::Error::new(
            io::ErrorKind::UnexpectedEof,
            "unexpected end of NCM stream",
        )));
    }
    Ok(())
}

/// Fill `buf`, tolerating a short read at EOF.
fn read_up_to<R: Read>(r: &mut R, buf: &mut [u8]) -> Result<usize> {
    let mut n = 0;
    while n < buf.len() {
        match r.read(&mut buf[n..]) {
            Ok(0) => break,
            Ok(m) => n += m,
            Err(e) if e.kind() == io::ErrorKind::Interrupted => continue,
            Err(e) => return Err(Error::Io(e)),
        }
    }
    Ok(n)
}

fn detect_format(b: &[u8]) -> Format {
    match b {
        _ if b.len() >= 4 && &b[..4] == b"fLaC" => Format::Flac,
        _ if b.len() >= 4 && &b[..4] == b"OggS" => Format::Ogg,
        _ if b.len() >= 3 && &b[..3] == b"ID3" => Format::Mp3,
        // Both MPEG families share the 11-bit 0xFFE sync; the two layer bits
        // tell them apart (ADTS layer 00, MPEG audio layer 01/10/11).
        _ if b.len() >= 2 && b[0] == 0xFF && b[1] & 0xE0 == 0xE0 && b[1] & 0x06 != 0 => Format::Mp3,
        _ if b.len() >= 2 && b[0] == 0xFF && b[1] & 0xF6 == 0xF0 => Format::Aac,
        _ if b.len() >= 12 && &b[4..8] == b"ftyp" && is_m4a_brand(&b[8..12]) => Format::M4a,
        _ => Format::Unknown,
    }
}

/// True when a 4-byte `ftyp` brand denotes an MPEG-4 audio container.
fn is_m4a_brand(brand: &[u8]) -> bool {
    brand == b"M4A " || brand == b"M4B " || brand == b"mp41" || brand == b"mp42" || brand == b"isom"
}

#[cfg(test)]
mod tests {
    use super::*;

    fn ftyp(brand: &[u8; 4]) -> [u8; 12] {
        let mut b = [0u8; 12];
        b[4..8].copy_from_slice(b"ftyp");
        b[8..12].copy_from_slice(brand);
        b
    }

    #[test]
    fn sniffs_known_formats() {
        assert_eq!(detect_format(b"fLaC\x00\x00\x00\x22"), Format::Flac);
        assert_eq!(detect_format(b"OggS\x00\x02"), Format::Ogg);
        assert_eq!(detect_format(b"ID3\x04\x00"), Format::Mp3);
        assert_eq!(detect_format(&[0xFF, 0xFB, 0x90]), Format::Mp3);
        assert_eq!(detect_format(&[0xFF, 0xE3, 0x00]), Format::Mp3); // MPEG-2.5
        assert_eq!(detect_format(&[0xFF, 0xF1, 0x50]), Format::Aac);
        assert_eq!(detect_format(&[0xFF, 0xF9, 0x50]), Format::Aac);
        assert_eq!(detect_format(&ftyp(b"M4A ")), Format::M4a);
        assert_eq!(detect_format(&ftyp(b"mp42")), Format::M4a);
        assert_eq!(detect_format(&ftyp(b"isom")), Format::M4a);
        assert_eq!(detect_format(b"not-a-known-header"), Format::Unknown);
        assert_eq!(detect_format(&[0xFF]), Format::Unknown);
        assert_eq!(detect_format(&[]), Format::Unknown);
    }
}
