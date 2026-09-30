//! NCM keystream.
//!
//! The audio cipher is *not* the standard RC4 PRGA. For each byte index `i`
//! (mod 256) the keystream byte is:
//!
//! ```text
//! j     = i + 1
//! k2    = j + box[j]
//! out   = box[box[j] + box[k2]]
//! ```
//!
//! Because it only depends on `i mod 256`, we precompute a 256-byte table and
//! cycle it over the audio — same result, no per-byte branches.

/// Build the 256-byte RC4 state box from the box key (KSA).
pub(crate) fn key_box(key: &[u8]) -> [u8; 256] {
    let mut s = [0u8; 256];
    for (i, v) in s.iter_mut().enumerate() {
        *v = i as u8;
    }

    let mut j = 0u8;
    let mut kp = 0usize;
    for i in 0..256 {
        j = j.wrapping_add(s[i]).wrapping_add(key[kp]);
        kp = (kp + 1) % key.len();
        s.swap(i, j as usize);
    }
    s
}

/// Precompute the 256-byte NCM keystream table.
pub(crate) fn keystream(s: &[u8; 256]) -> [u8; 256] {
    let mut t = [0u8; 256];
    for (i, slot) in t.iter_mut().enumerate() {
        let j = (i as u8).wrapping_add(1);
        let k2 = j.wrapping_add(s[j as usize]);
        let idx = s[j as usize].wrapping_add(s[k2 as usize]);
        *slot = s[idx as usize];
    }
    t
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn ksa_produces_a_permutation() {
        let s = key_box(b"hello-key");
        let mut seen = [false; 256];
        for &b in &s {
            assert!(!seen[b as usize], "duplicate byte {b}");
            seen[b as usize] = true;
        }
    }

    #[test]
    fn keystream_matches_direct_formula() {
        let key: Vec<u8> = (0u8..=255).collect();
        let s = key_box(&key);
        let t = keystream(&s);
        for (i, &got) in t.iter().enumerate() {
            let j = (i as u8).wrapping_add(1);
            let a = s[j as usize];
            let k2 = j.wrapping_add(a);
            let want = s[a.wrapping_add(s[k2 as usize]) as usize];
            assert_eq!(got, want, "at i={i}");
        }
    }
}
