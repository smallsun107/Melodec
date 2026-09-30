#pragma once

// ReNcm - NCM (*.ncm) decoder.
//
// The format and algorithms were recovered by reverse engineering the
// NeteaseMusic macOS binary. The full write-up - IDA addresses, field layout,
// call chain - is in docs/NCM-FORMAT-ANALYSIS.md.
//
// Pipeline:
//
//	key_blob --XOR 0x64--> AES-128-ECB(core key) --> PKCS#7 unpad
//	         --> strip "neteasecloudmusic"  ==> box key
//	box   = RC4-KSA(box key)
//	table = precomputed 256-byte NCM keystream, cycled over the audio
//	audio[i] ^= table[i % 256]
//	comment --XOR 0x63--> base64 --> AES-128-ECB(meta key) --> JSON
//
// Everything in src/ (aes, rc4, key, meta, format detection) is an
// implementation detail and is not part of this header.

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace rencm {

/// Decoded audio container format.
enum class Format { Mp3, Flac, M4a, Ogg, Aac, Unknown };

/// Usual file extension, without a leading dot.
const char* format_ext(Format f);

/// One entry of the metadata `artist` array.
struct Artist {
    std::string name;
    int64_t id = 0;
    bool has_id = false;
};

/// Parsed metadata JSON (`music:{...}`).
struct Metadata {
    bool present = false;
    int64_t music_id = 0;
    std::string music_name;
    std::vector<Artist> artists;
    std::string album;
    int64_t album_id = 0;
    int bitrate = 0;
    int64_t duration_ms = 0;
    /// 文件自述的格式，来自内嵌元数据 JSON 的 "format" 字段；
    /// 与 Result::format（从音频字节实测）可能不一致。
    std::string format;
    std::string raw_json;
};

/// Everything decoded from one .ncm file.
struct Result {
    uint8_t version = 0;
    /// 从音频字节实测的格式（嗅探音频流开头的 fLaC/ID3/OggS/…）；
    /// 与 Metadata::format（文件自述）可能不一致。
    Format format = Format::Unknown;
    std::vector<uint8_t> box_key;
    std::vector<uint8_t> cover;
    Metadata meta;
    std::vector<uint8_t> audio;
};

/// Progress callback: (bytes done, bytes total) of the audio payload.
using ProgressFn = std::function<void(uint64_t done, uint64_t total)>;

/// Decode a whole .ncm file into memory. Returns false and sets `err` on failure.
bool decode_file(const std::string& path, Result& out, std::string& err,
                 const ProgressFn& progress = {});

/// Decode and write the audio (and optionally the cover) next to the input, or
/// into `out_dir` when non-empty. On success `written_audio` is the audio path.
bool decode_to_file(const std::string& path, const std::string& out_dir, bool write_cover,
                    Result& out, std::string& written_audio, std::string& err,
                    const ProgressFn& progress = {});

} // namespace rencm
