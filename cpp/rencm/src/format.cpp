#include "format.hpp"
#include "rencm/rencm.hpp"

#include <cstring>

namespace rencm {

const char* format_ext(Format f) {
    switch (f) {
        case Format::Mp3: return "mp3";
        case Format::Flac: return "flac";
        case Format::M4a: return "m4a";
        case Format::Ogg: return "ogg";
        case Format::Aac: return "aac";
        default: return "bin";
    }
}

namespace internal {

Format detect_format(const uint8_t* b, std::size_t size) {
    auto has = [&](std::size_t off, const char* m) {
        const std::size_t n = std::strlen(m);
        return size >= off + n && std::memcmp(b + off, m, n) == 0;
    };
    if (has(0, "fLaC")) return Format::Flac;
    if (has(0, "OggS")) return Format::Ogg;
    if (has(0, "ID3")) return Format::Mp3;
    // Both MPEG families share the 11-bit 0xFFE sync; the two layer bits tell
    // them apart (ADTS layer 00, MPEG audio layer 01/10/11).
    if (size >= 2 && b[0] == 0xFF && (b[1] & 0xE0) == 0xE0 && (b[1] & 0x06) != 0)
        return Format::Mp3;
    if (size >= 2 && b[0] == 0xFF && (b[1] & 0xF6) == 0xF0) return Format::Aac;
    if (size >= 12 && std::memcmp(b + 4, "ftyp", 4) == 0 &&
        (has(8, "M4A ") || has(8, "M4B ") || has(8, "mp41") || has(8, "mp42") || has(8, "isom")))
        return Format::M4a;
    return Format::Unknown;
}

} // namespace internal
} // namespace rencm
