# rencm (C++)

NCM (`*.ncm`) decoder library. No third-party dependencies, so it can be dropped
into another project:

```cmake
add_subdirectory(rencm)
target_link_libraries(myapp PRIVATE rencm::rencm)
```

## API

```cpp
#include <rencm/rencm.hpp>

rencm::Result r;
std::string err, out;

// decode into memory
if (!rencm::decode_file("song.ncm", r, err)) {
    fprintf(stderr, "%s\n", err.c_str());
}

// or decode and write <stem>.<ext> (+ .jpg) next to the source / into out_dir
if (!rencm::decode_to_file("song.ncm", /*out_dir=*/"", /*write_cover=*/true, r, out, err)) {
    fprintf(stderr, "%s\n", err.c_str());
}

// progress callback: (bytes done, bytes total) of the audio payload
rencm::decode_file("song.ncm", r, err, [](uint64_t d, uint64_t t) { /* ... */ });
```

`Result` carries `version`, `format`, `box_key`, `cover`, `audio` and `meta`
(`Metadata` with typed accessors: `music_name`, `artists`, `album`, `bitrate`,
`duration_ms`, `format`, plus the raw JSON in `meta.raw_json`).

## Layout

| Path | Contents |
| --- | --- |
| `include/rencm/rencm.hpp` | the **entire** public API, one header |
| `src/` | implementation; everything here is `rencm::internal` and not part of the public API |

Internal files: `aes` (AES-128-ECB), `rc4` (KSA + keystream), `key` (section
decryption), `meta` (JSON extraction), `format` (`format_ext` +
`internal::detect_format`), `decoder` (container parsing, audio decryption, writing).
Only `include/rencm` is added to the include path of consumers, so the `src/`
headers can use natural names (`format.hpp`, `meta.hpp`, …) without collision.

## Algorithm

See `../gui/assets/fonts/README.md` for the fonts and `NCM-FORMAT-ANALYSIS.md` under
`docs/` for the full reverse-engineering write-up (format layout, key
derivation, keystream, metadata).

```text
key_blob --XOR 0x64--> AES-128-ECB(core key) --> PKCS#7 unpad
         --> strip "neteasecloudmusic"  ==> box key
box   = RC4-KSA(box key)
table = precomputed 256-byte NCM keystream, cycled over the audio
audio[i] ^= table[i % 256]
comment --XOR 0x63--> base64 --> AES-128-ECB(meta key) --> JSON
```
