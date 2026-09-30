// Golden decode against a real sample (.ncm). Skipped unless RENCM_SAMPLE is
// set, so the unit tests stay runnable without the (large, uncommitted) file:
//
//   RENCM_SAMPLE=/path/sample.ncm cargo test

use std::io::Read;

use sha2::{Digest, Sha256};

fn sha256(bytes: &[u8]) -> String {
    let digest = Sha256::digest(bytes);
    let mut s = String::with_capacity(digest.len() * 2);
    for b in digest {
        s.push_str(&format!("{b:02x}"));
    }
    s
}

#[test]
fn golden_sample() {
    let Ok(path) = std::env::var("RENCM_SAMPLE") else {
        eprintln!("SKIP: set RENCM_SAMPLE to a .ncm file to run the golden decode");
        return;
    };

    let mut dec = rencm::open(&path).expect("open sample");
    assert_eq!(dec.version(), 1, "version");
    assert_eq!(dec.format().ext(), "mp3", "format");
    assert_eq!(dec.box_key().len(), 112, "box_key length");
    assert_eq!(dec.cover().map(<[u8]>::len), Some(38969), "cover length");
    assert_eq!(
        sha256(dec.box_key()),
        "7e07a6ecb23fd345ef8fe390998d4496e951edaad52dc06b0c866ba7174ee685",
        "box_key sha256"
    );
    assert_eq!(
        sha256(dec.cover().unwrap()),
        "05a4de5aafc664c9843a7f808e17185d5eff0f7b88969f32af1fa44c8651e71f",
        "cover sha256"
    );

    let mut audio = Vec::new();
    dec.read_to_end(&mut audio).expect("read audio");
    assert_eq!(audio.len(), 7448076, "audio length");
    assert_eq!(
        sha256(&audio),
        "ea30a900af1843433ff24459c0351b7076508d489296245ac1c2c9865c9d0026",
        "audio sha256"
    );
}
