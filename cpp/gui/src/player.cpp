// rencm-gui - player backend and visualizer source.
//
// The ONLY translation unit that compiles miniaudio - including the header
// without MINIAUDIO_IMPLEMENTATION elsewhere gives declarations only.
//
// Frames come from ma_decoder and go straight to ma_device in the audio
// callback; the same callback mirrors a mono copy into a small ring buffer that
// update_spectrum() analyses on the UI thread.

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include "player.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace rencm::app {

namespace {

constexpr ma_uint32 kChannels = 2;
constexpr ma_uint32 kSampleRate = 48000;
constexpr float kPi = 3.14159265358979323846f;

// Visualizer band range: log-spaced from 40 Hz to 16 kHz.
constexpr float kLowHz = 40.0f;
constexpr float kHighHz = 16000.0f;
constexpr float kFloorDb = -52.0f;

void audio_callback(ma_device* device, void* output, const void* /*input*/, ma_uint32 frame_count) {
    player_render(*static_cast<Player*>(device->pUserData), static_cast<float*>(output), frame_count);
}

} // namespace

// ---------------------------------------------------------------------------
// audio thread
// ---------------------------------------------------------------------------

void player_render(Player& self, float* out, unsigned int frame_count) {
    const std::size_t samples = std::size_t(frame_count) * kChannels;
    if (!self.playing_.load() || self.decoder_ == nullptr) {
        std::memset(out, 0, samples * sizeof(float));
        return;
    }

    if (self.seek_pending_.exchange(false))
        ma_decoder_seek_to_pcm_frame(self.decoder_, self.seek_frame_.load());

    ma_uint64 got = 0;
    ma_decoder_read_pcm_frames(self.decoder_, out, frame_count, &got);
    if (got < frame_count)
        std::memset(out + got * kChannels, 0,
                    std::size_t(frame_count - got) * kChannels * sizeof(float));

    // Mirror the pre-gain mono signal for the visualizer, so lowering the
    // volume does not flatten the bars.
    std::uint32_t head = self.ring_head_.load(std::memory_order_relaxed);
    for (ma_uint32 i = 0; i < got; ++i) {
        self.ring_[head & std::uint32_t(Player::kRingSize - 1)] = 0.5f * (out[i * 2] + out[i * 2 + 1]);
        ++head;
    }
    self.ring_head_.store(head, std::memory_order_relaxed);

    const float gain = self.volume_.load();
    for (std::size_t i = 0; i < samples; ++i) out[i] *= gain;

    ma_uint64 cursor = 0;
    if (ma_decoder_get_cursor_in_pcm_frames(self.decoder_, &cursor) == MA_SUCCESS)
        self.cursor_.store(cursor);

    if (got == 0) {
        self.at_end_.store(true);
        self.playing_.store(false);
    }
}

// ---------------------------------------------------------------------------
// UI thread
// ---------------------------------------------------------------------------

Player::Player() {
    device_ = new ma_device;
    ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
    cfg.playback.format = ma_format_f32;
    cfg.playback.channels = kChannels;
    cfg.sampleRate = kSampleRate;
    cfg.dataCallback = audio_callback;
    cfg.pUserData = this;

    const ma_result r = ma_device_init(nullptr, &cfg, device_);
    if (r != MA_SUCCESS) {
        delete device_;
        device_ = nullptr;
        error_ = ma_result_description(r);
    }
}

Player::~Player() {
    if (device_ != nullptr) {
        ma_device_stop(device_);
        ma_device_uninit(device_);
        delete device_;
    }
    if (decoder_ != nullptr) {
        ma_decoder_uninit(decoder_);
        delete decoder_;
    }
}

void Player::unload() {
    if (device_ != nullptr) ma_device_stop(device_); // waits for the callback
    if (decoder_ != nullptr) {
        ma_decoder_uninit(decoder_);
        delete decoder_;
        decoder_ = nullptr;
    }
    playing_.store(false);
    at_end_.store(false);
    seek_pending_.store(false);
    cursor_.store(0);
    duration_ = 0.0;
    path_.clear();
    loaded_ = false;
}

bool Player::load(const std::string& path) {
    unload();
    // Keep the device error from the constructor until we can report it.
    if (device_ == nullptr) return false;
    error_.clear();

    const ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, kChannels, kSampleRate);
    decoder_ = new ma_decoder;
    const ma_result r = ma_decoder_init_file(path.c_str(), &cfg, decoder_);
    if (r != MA_SUCCESS) {
        delete decoder_;
        decoder_ = nullptr;
        error_ = ma_result_description(r);
        return false;
    }

    ma_uint64 frames = 0;
    if (ma_decoder_get_length_in_pcm_frames(decoder_, &frames) == MA_SUCCESS)
        duration_ = double(frames) / double(kSampleRate);

    path_ = path;
    loaded_ = true;
    return true;
}

void Player::play() {
    if (device_ == nullptr || decoder_ == nullptr) return;
    if (at_end_.load()) { // restart from the top
        seek_frame_.store(0);
        seek_pending_.store(true);
        at_end_.store(false);
    }
    playing_.store(true);
    if (ma_device_start(device_) != MA_SUCCESS) playing_.store(false);
}

void Player::pause() {
    if (device_ == nullptr) return;
    playing_.store(false);
    ma_device_stop(device_);
}

void Player::toggle() {
    if (playing())
        pause();
    else
        play();
}

double Player::position() const {
    return double(cursor_.load()) / double(kSampleRate);
}

void Player::seek(double seconds) {
    if (decoder_ == nullptr) return;
    if (seconds < 0.0) seconds = 0.0;
    if (duration_ > 0.0 && seconds > duration_) seconds = duration_;

    seek_frame_.store(std::uint64_t(seconds * double(kSampleRate)));
    seek_pending_.store(true);
    at_end_.store(false);

    // While stopped no callback will pick the request up, so apply it here.
    if (!playing_.load() && seek_pending_.exchange(false)) {
        ma_decoder_seek_to_pcm_frame(decoder_, seek_frame_.load());
        cursor_.store(seek_frame_.load());
    }
}

void Player::set_volume(float v) {
    volume_.store(std::clamp(v, 0.0f, 1.0f));
}

void Player::update_spectrum() {
    // Window and Goertzel coefficients never change - built once.
    struct Tables {
        float window[kFftN];
        float coeff[kSpectrumBins];
        Tables() {
            for (int i = 0; i < kFftN; ++i)
                window[i] = 0.5f * (1.0f - std::cos(2.0f * kPi * float(i) / float(kFftN - 1)));
            for (int b = 0; b < kSpectrumBins; ++b) {
                const float t = float(b) / float(kSpectrumBins - 1);
                const float hz = kLowHz * std::pow(kHighHz / kLowHz, t);
                coeff[b] = 2.0f * std::cos(2.0f * kPi * hz / float(kSampleRate));
            }
        }
    };
    static const Tables tables;

    const std::uint32_t head = ring_head_.load(std::memory_order_relaxed);
    float buf[kFftN];
    for (int i = 0; i < kFftN; ++i) {
        const std::uint32_t idx = head - std::uint32_t(kFftN) + std::uint32_t(i);
        buf[i] = ring_[idx & std::uint32_t(kRingSize - 1)] * tables.window[i];
    }

    for (int b = 0; b < kSpectrumBins; ++b) {
        const float c = tables.coeff[b];
        float s1 = 0.0f, s2 = 0.0f;
        for (int i = 0; i < kFftN; ++i) {
            const float s0 = buf[i] + c * s1 - s2;
            s2 = s1;
            s1 = s0;
        }
        const float power = s1 * s1 + s2 * s2 - c * s1 * s2;
        const float mag = std::sqrt(std::max(power, 0.0f)) / float(kFftN);
        const float db = 20.0f * std::log10(mag + 1e-7f);
        const float v = std::clamp((db - kFloorDb) / -kFloorDb, 0.0f, 1.0f);
        // Lift the mids a touch so quiet passages still move, then fast attack
        // and slow release.
        const float shaped = std::pow(v, 0.75f);
        smooth_[b] = std::max(shaped, smooth_[b] * 0.80f);
        spectrum_[b] = smooth_[b];
    }
}

} // namespace rencm::app
