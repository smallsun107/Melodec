#include "rencm/rencm.hpp"

#include "format.hpp"
#include "key.hpp"
#include "meta.hpp"
#include "rc4.hpp"

#include <cstring>
#include <filesystem>
#include <fstream>

namespace rencm {
namespace {

namespace fs = std::filesystem;

constexpr const char* kMagic = "CTENFDAM";
constexpr uint64_t kProgressStep = 1u << 18;  // report every 256 KiB

uint32_t rd_u32(const std::vector<uint8_t>& b, std::size_t off) {
    return (uint32_t)b[off] | ((uint32_t)b[off + 1] << 8) | ((uint32_t)b[off + 2] << 16) |
           ((uint32_t)b[off + 3] << 24);
}

bool read_whole_file(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    const std::streamoff n = f.tellg();
    if (n < 0) return false;
    f.seekg(0, std::ios::beg);
    out.resize((std::size_t)n);
    if (n > 0) f.read((char*)out.data(), n);
    return (bool)f;
}

bool write_whole_file(const std::string& path, const std::vector<uint8_t>& data) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    if (!data.empty()) f.write((const char*)data.data(), (std::streamsize)data.size());
    return (bool)f;
}

bool decode_file_impl(const std::string& path, Result& out, std::string& err,
                      const ProgressFn& progress) {
    out = Result{};  // a reused Result must not leak state from a previous decode
    std::vector<uint8_t> raw;
    if (!read_whole_file(path, raw)) {
        err = "cannot read file";
        return false;
    }
    if (raw.size() < 14 || std::memcmp(raw.data(), kMagic, 8) != 0) {
        err = "not a valid NCM file (bad magic)";
        return false;
    }

    out.version = raw[8];
    std::size_t p = 10;
    const uint32_t key_len = rd_u32(raw, p);
    p += 4;
    if (p + key_len > raw.size()) {
        err = "key section out of range";
        return false;
    }
    const std::vector<uint8_t> key_blob(raw.begin() + p, raw.begin() + p + key_len);
    p += key_len;

    if (!internal::decrypt_key(key_blob, out.box_key, err)) return false;

    if (p + 4 > raw.size()) {
        err = "metadata length out of range";
        return false;
    }
    const uint32_t meta_len = rd_u32(raw, p);
    p += 4;
    if (p + meta_len > raw.size()) {
        err = "metadata section out of range";
        return false;
    }
    const std::vector<uint8_t> meta_blob(raw.begin() + p, raw.begin() + p + meta_len);
    p += meta_len;

    if (!meta_blob.empty()) {
        std::string json, meta_err;
        if (internal::decrypt_comment(meta_blob, json, meta_err))
            out.meta = internal::parse_metadata(json);
        // metadata is optional: a failure here is not fatal
    }

    // cover section: crc(4) + flag(1) + cover_size u32 + duplicate u32, then data.
    // The first size is authoritative (audio starts right after it); a larger
    // duplicate would misalign the audio, so reject that rather than emit
    // silently wrong bytes.
    if (p + 13 > raw.size()) {
        err = "trailer out of range";
        return false;
    }
    p += 5;
    const uint32_t cover_span = rd_u32(raw, p); // padded span; the audio starts this far in
    p += 4;
    const uint32_t cover_len = rd_u32(raw, p); // actual cover bytes, right here
    p += 4;
    if (cover_len > cover_span) {
        err = "inconsistent cover size";
        return false;
    }
    if (p + cover_span > raw.size()) {
        err = "cover section out of range";
        return false;
    }
    // The cover starts *at* the current position and is cover_len bytes; cover_span
    // may be larger, the difference being padding before the audio. (Reading
    // after skipping that padding - which this used to do - slices the image and
    // yields garbage whenever the two sizes differ.)
    if (cover_len > 0) out.cover.assign(raw.begin() + p, raw.begin() + p + cover_len);
    p += cover_span;

    if (p > raw.size()) {
        err = "audio offset out of range";
        return false;
    }
    out.audio.assign(raw.begin() + p, raw.end());

    uint8_t box[256];
    uint8_t table[256];
    internal::key_box(out.box_key.data(), out.box_key.size(), box);
    internal::keystream(box, table);

    const uint64_t total = (uint64_t)out.audio.size();
    uint64_t next = kProgressStep;
    if (progress) progress(0, total);
    for (std::size_t i = 0; i < out.audio.size(); ++i) {
        out.audio[i] ^= table[i & 0xFF];
        if (progress && (uint64_t)i >= next) {
            progress((uint64_t)i, total);
            next += kProgressStep;
        }
    }
    if (progress) progress(total, total);

    out.format = internal::detect_format(out.audio.data(), out.audio.size());
    return true;
}

bool decode_to_file_impl(const std::string& path, const std::string& out_dir, bool write_cover,
                         Result& out, std::string& written_audio, std::string& err,
                         const ProgressFn& progress) {
    if (!decode_file(path, out, err, progress)) return false;

    const fs::path in(path);
    fs::path dir = out_dir.empty() ? in.parent_path() : fs::path(out_dir);
    if (dir.empty()) dir = ".";
    const std::string stem = in.stem().string();

    std::error_code ec;
    if (!out_dir.empty()) fs::create_directories(dir, ec);

    const fs::path audio_path = dir / (stem + "." + format_ext(out.format));
    if (!write_whole_file(audio_path.string(), out.audio)) {
        err = "failed to write audio: " + audio_path.string();
        return false;
    }
    written_audio = audio_path.string();

    if (write_cover && !out.cover.empty()) {
        const fs::path cover_path = dir / (stem + ".jpg");
        write_whole_file(cover_path.string(), out.cover);
    }
    return true;
}

} // namespace

bool decode_file(const std::string& path, Result& out, std::string& err, const ProgressFn& progress) {
    try {
        return decode_file_impl(path, out, err, progress);
    } catch (const std::exception& e) {
        err = std::string("decode failed: ") + e.what();
        return false;
    } catch (...) {
        err = "decode failed: unknown exception";
        return false;
    }
}

bool decode_to_file(const std::string& path, const std::string& out_dir, bool write_cover,
                    Result& out, std::string& written_audio, std::string& err,
                    const ProgressFn& progress) {
    try {
        return decode_to_file_impl(path, out_dir, write_cover, out, written_audio, err, progress);
    } catch (const std::exception& e) {
        err = std::string("decode failed: ") + e.what();
        return false;
    } catch (...) {
        err = "decode failed: unknown exception";
        return false;
    }
}

} // namespace rencm
