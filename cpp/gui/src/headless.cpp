#include "headless.hpp"

#include "cover.hpp"
#include "player.hpp"
#include "tasks.hpp"

#include <rencm/rencm.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

namespace rencm::app {

namespace fs = std::filesystem;

int run_headless(int argc, char** argv) {
    std::vector<std::string> inputs(argv + 1, argv + argc);
    std::string out_dir;
    // optional trailing output directory (also accepts a not-yet-created path)
    if (inputs.size() >= 2) {
        const std::string& last = inputs.back();
        std::error_code ec;
        if (fs::is_directory(last, ec) || !fs::exists(last, ec)) {
            out_dir = last;
            inputs.pop_back();
        }
    }

    const auto tasks = collect_tasks(inputs, true);
    if (tasks.empty()) {
        std::fprintf(stderr, "no .ncm files found\n");
        return 1;
    }

    int failures = 0;
    for (const auto& t : tasks) {
        const std::string& f = t.path;
        rencm::Result r;
        std::string err, out;
        if (rencm::decode_to_file(f, dest_dir_for(t, out_dir), true, r, out, err)) {
            std::printf("%s\n", f.c_str());
            std::printf("  version   : %u\n", r.version);
            std::printf("  format    : %s\n", rencm::format_ext(r.format));
            std::printf("  box_key   : %zu bytes\n", r.box_key.size());
            std::printf("  audio     : %zu bytes\n", r.audio.size());
            std::printf("  output    : %s\n", out.c_str());
            if (!r.cover.empty()) {
                fs::path cover_path(out);
                cover_path.replace_extension(".jpg");
                std::printf("  cover_out : %s\n", cover_path.string().c_str());
            }
            if (r.meta.present)
                std::printf("  metadata  : %s\n", r.meta.raw_json.c_str());
        } else {
            std::printf("%s\n  ERROR: %s\n", f.c_str(), err.c_str());
            ++failures;
        }
    }
    return failures == 0 ? 0 : 1;
}

int run_playtest(const char* path) {
    Player p;
    if (!p.load(path)) {
        std::fprintf(stderr, "load failed: %s\n", p.error().c_str());
        return 1;
    }

    const double dur = p.duration();
    std::printf("path      : %s\n", p.path().c_str());
    std::printf("duration  : %.3f s\n", dur);
    if (dur <= 0.0) {
        std::fprintf(stderr, "FAIL: no duration reported\n");
        return 1;
    }

    p.play();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    const double pos_a = p.position();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    const double pos_b = p.position();
    std::printf("playing   : %d\n", p.playing() ? 1 : 0);
    std::printf("position  : %.3f -> %.3f s\n", pos_a, pos_b);

    // Visualizer path: analyse the samples the audio callback mirrored.
    float peak = 0.0f;
    for (int k = 0; k < 5; ++k) {
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
        p.update_spectrum();
        for (int i = 0; i < kSpectrumBins; ++i) peak = std::max(peak, p.spectrum()[i]);
    }
    std::printf("spectrum  : peak %.3f\n", peak);

    const double target = dur / 2.0;
    p.seek(target);
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    const double after = p.position();
    std::printf("seek(%.3f): %.3f s\n", target, after);

    p.pause();
    std::printf("paused    : playing=%d\n", p.playing() ? 1 : 0);

    const bool advanced = pos_b > pos_a;
    const bool sought = after > target - 1.0 && after < target + 1.0;
    const bool visual = peak > 0.0f;
    if (!advanced || !sought || !visual) {
        std::fprintf(stderr, "FAIL: advanced=%d sought=%d spectrum=%d\n", advanced ? 1 : 0,
                     sought ? 1 : 0, visual ? 1 : 0);
        return 1;
    }
    std::printf("OK\n");
    return 0;
}

int run_covercheck(const char* path) {
    CoverImage img;
    if (!decode_cover(path, img)) {
        std::fprintf(stderr, "decode failed: %s\n", path);
        return 1;
    }

    // FNV-1a over the pixels: proves the buffer holds real image data rather
    // than a blank/zeroed decode.
    std::uint64_t hash = 1469598103934665603ull;
    for (std::uint8_t b : img.rgba) {
        hash ^= b;
        hash *= 1099511628211ull;
    }

    std::printf("path      : %s\n", path);
    std::printf("size      : %d x %d\n", img.width, img.height);
    std::printf("rgba      : %zu bytes\n", img.rgba.size());
    std::printf("hash      : %016llx\n", (unsigned long long)hash);

    const bool ok = img.width > 0 && img.height > 0 && !img.rgba.empty() &&
                    hash != 1469598103934665603ull;
    std::printf("%s\n", ok ? "OK" : "FAIL");
    return ok ? 0 : 1;
}

} // namespace rencm::app
