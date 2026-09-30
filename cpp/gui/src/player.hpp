#pragma once

#include <atomic>
#include <cstdint>
#include <string>

// Opaque miniaudio types - only player.cpp includes miniaudio.h.
struct ma_device;
struct ma_decoder;

namespace rencm::app {

/// Number of bars the visualizer draws.
inline constexpr int kSpectrumBins = 96;

/// Minimal cross-platform player for a file already on disk.
///
/// The decoder always writes its audio out, so this replays the resulting file.
/// Backed by miniaudio, which brings its own device backends (CoreAudio /
/// WASAPI / ALSA / PulseAudio) and decoders for mp3 / flac / wav; m4a / aac /
/// ogg are not decodable, so load() fails cleanly for those.
///
/// Playback goes straight through ma_device + ma_decoder rather than ma_engine:
/// the audio callback owns the decoded frames, which is what feeds the
/// visualizer. The spectrum is a per-bin Goertzel pass (~50k mul-adds a frame),
/// so no FFT library is pulled in.
class Player {
public:
    Player();
    ~Player();
    Player(const Player&) = delete;
    Player& operator=(const Player&) = delete;

    /// Load `path`, replacing anything already loaded. False (with error() set)
    /// when the file is missing or its container is not decodable.
    bool load(const std::string& path);

    void play();
    void pause();
    void toggle();

    bool loaded() const { return loaded_; }
    bool playing() const { return playing_.load() && !at_end_.load(); }
    bool ended() const { return at_end_.load(); }

    double position() const; ///< playback head, seconds
    double duration() const { return duration_; }
    void seek(double seconds);

    float volume() const { return volume_.load(); }
    void set_volume(float v);

    const std::string& path() const { return path_; }
    const std::string& error() const { return error_; }

    /// Recompute spectrum() from the most recent ~20 ms of audio. Call once per
    /// frame from the UI thread.
    void update_spectrum();

    /// kSpectrumBins smoothed magnitudes in [0,1].
    const float* spectrum() const { return spectrum_; }

private:
    // Implemented in player.cpp; runs on the miniaudio audio thread.
    friend void player_render(Player& self, float* out, unsigned int frame_count);

    static constexpr int kRingSize = 4096; ///< power of two
    static constexpr int kFftN = 1024;

    void unload();

    ma_device* device_ = nullptr;
    ma_decoder* decoder_ = nullptr;

    std::atomic<bool> playing_{false};
    std::atomic<bool> at_end_{false};
    std::atomic<bool> seek_pending_{false};
    std::atomic<std::uint64_t> seek_frame_{0};
    std::atomic<std::uint64_t> cursor_{0};
    std::atomic<float> volume_{1.0f};

    // Written by the audio thread, read by the UI for the visualizer.
    float ring_[kRingSize] = {};
    std::atomic<std::uint32_t> ring_head_{0};

    float spectrum_[kSpectrumBins] = {};
    float smooth_[kSpectrumBins] = {};

    double duration_ = 0.0;
    std::string path_;
    std::string error_;
    bool loaded_ = false;
};

} // namespace rencm::app
