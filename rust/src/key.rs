use aes::cipher::generic_array::GenericArray;
use aes::cipher::{BlockDecrypt, KeyInit};
use aes::Aes128;
use base64::{engine::general_purpose::STANDARD, Engine as _};

use crate::error::{Error, Result};

/// Box-key prefix stripped after decryption.
pub(crate) const KEY_PREFIX: &[u8] = b"neteasecloudmusic";

/// AES-128 key for the key section ("core key").
const CORE_KEY: &[u8; 16] = b"hzHRAmso5kInbaxW";
/// AES-128 key for the comment/metadata section.
const META_KEY: &[u8; 16] = b"#14ljk_!\\]&0U<'(";

const META_MARKER: &[u8] = b"163 key(Don't modify):";
const META_PREFIX: &[u8] = b"music:";

/// AES-128-ECB decrypt of whole 16-byte blocks.
fn aes_ecb_decrypt(data: &[u8], key: &[u8]) -> Vec<u8> {
    let cipher = Aes128::new(GenericArray::from_slice(key));
    let n = data.len() - data.len() % 16;
    let mut out = vec![0u8; n];
    for i in (0..n).step_by(16) {
        let mut block = GenericArray::clone_from_slice(&data[i..i + 16]);
        cipher.decrypt_block(&mut block);
        out[i..i + 16].copy_from_slice(&block);
    }
    out
}

/// Remove PKCS#7 padding when valid, otherwise return the input unchanged.
fn pkcs7_unpad(d: &[u8]) -> &[u8] {
    if let Some(&pad) = d.last() {
        let pad = pad as usize;
        if (1..=16).contains(&pad)
            && pad <= d.len()
            && d[d.len() - pad..].iter().all(|&b| b as usize == pad)
        {
            return &d[..d.len() - pad];
        }
    }
    d
}

fn trim_ascii_ws(s: &[u8]) -> &[u8] {
    let is_ws = |b: u8| b == b' ' || b == b'\t' || b == b'\r' || b == b'\n';
    let start = s.iter().position(|&b| !is_ws(b)).unwrap_or(s.len());
    let end = s.iter().rposition(|&b| !is_ws(b)).map_or(start, |i| i + 1);
    &s[start..end]
}

/// Derive the box key from the raw key section.
///
/// `XOR 0x64 -> AES-128-ECB(core key) -> PKCS#7 unpad -> strip prefix`
pub(crate) fn decrypt_key(blob: &[u8]) -> Result<Vec<u8>> {
    let xored: Vec<u8> = blob.iter().map(|b| b ^ 0x64).collect();
    let plain = aes_ecb_decrypt(&xored, CORE_KEY);
    let plain = pkcs7_unpad(&plain);

    if !plain.starts_with(KEY_PREFIX) {
        return Err(Error::BadKey);
    }
    let key = plain[KEY_PREFIX.len()..].to_vec();
    if key.is_empty() {
        return Err(Error::BadKey);
    }
    Ok(key)
}

/// Decode the raw comment section into the metadata JSON (without `music:`).
pub(crate) fn decrypt_comment(comment: &[u8]) -> Result<Vec<u8>> {
    let xored: Vec<u8> = comment.iter().map(|b| b ^ 0x63).collect();

    if !xored.starts_with(META_MARKER) {
        return Err(Error::BadComment);
    }

    let mut seg = &xored[META_MARKER.len()..];
    if let Some(i) = seg.iter().position(|&b| b == 0) {
        seg = &seg[..i]; // the payload is NUL-padded
    }
    let seg = trim_ascii_ws(seg);
    let mut b64 = seg.to_vec();
    while !b64.len().is_multiple_of(4) {
        b64.push(b'=');
    }

    let raw = STANDARD.decode(&b64)?;
    let plain = aes_ecb_decrypt(&raw, META_KEY);
    let plain = pkcs7_unpad(&plain);

    if !plain.starts_with(META_PREFIX) {
        return Err(Error::BadMeta);
    }
    Ok(plain[META_PREFIX.len()..].to_vec())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn unpads_valid_and_rejects_invalid() {
        assert_eq!(pkcs7_unpad(&[1, 2, 3, 3, 3, 3]), &[1, 2, 3]);
        assert_eq!(pkcs7_unpad(&[9, 1]), &[9]);
        assert_eq!(pkcs7_unpad(&[1, 2, 3, 3, 4, 3]), &[1, 2, 3, 3, 4, 3]);
        assert_eq!(pkcs7_unpad(&[1, 2, 0]), &[1, 2, 0]);
        assert_eq!(pkcs7_unpad(&[]), &[] as &[u8]);
    }

    #[test]
    fn trims_only_ascii_whitespace() {
        assert_eq!(trim_ascii_ws(b"  abc\t"), b"abc");
        assert_eq!(trim_ascii_ws(b"\r\nabc\n"), b"abc");
        assert_eq!(trim_ascii_ws(b"   "), b"");
    }

    #[test]
    fn comment_cuts_at_nul_before_base64() {
        // marker + valid base64 + NUL + trailing junk. If the NUL split worked,
        // the junk never reaches base64 (which would fail) and we get BadMeta
        // from the too-short plaintext instead.
        let mut plain = Vec::new();
        plain.extend_from_slice(META_MARKER);
        plain.extend_from_slice(b"AAAA");
        plain.push(0);
        plain.extend_from_slice(b"!!!! not base64");
        let blob: Vec<u8> = plain.iter().map(|b| b ^ 0x63).collect();

        match decrypt_comment(&blob) {
            Err(Error::BadMeta) => {}
            other => panic!("expected BadMeta, got {other:?}"),
        }
    }
}
