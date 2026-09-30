#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace rencm::app {

/// An image decoded to RGBA, row-major, 4 bytes per pixel.
struct CoverImage {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;
};

/// Decode a cover image from disk (JPEG or PNG). Split out from Cover so
/// `--covercheck` can verify decoding without a GL context.
bool decode_cover(const std::string& path, CoverImage& out);

/// One-slot GL texture cache for the cover of the track being played.
///
/// The decoder already writes the cover next to the audio as `<stem>.jpg`, so
/// the player just reads that file when the track changes - no cover bytes are
/// kept in memory by the job list.
class Cover {
public:
    Cover() = default;
    ~Cover();
    Cover(const Cover&) = delete;
    Cover& operator=(const Cover&) = delete;

    /// Texture for the sibling `.jpg` of `audio_path`, loading it when the track
    /// changed. Returns 0 when there is no cover or it failed to decode.
    unsigned int get(const std::string& audio_path);

private:
    unsigned int tex_ = 0;
    std::string path_;
};

} // namespace rencm::app
