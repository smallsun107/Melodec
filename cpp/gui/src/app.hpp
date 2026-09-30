#pragma once

// Shared application state for the rencm-gui front end.
//
// The GUI is split across several translation units - ui.cpp (layout + main
// loop), jobs.cpp (background decryption), browser.cpp (file-picker modal),
// playerbar.cpp (bottom transport) and theme.cpp - which all operate on the
// single App instance defined here.

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "cover.hpp"
#include "player.hpp"

namespace rencm::app {

/// One decrypted file, as shown in the results table.
struct Job {
    std::string path;
    bool ok = false;
    std::string message;
    std::string output;
    std::string title;
    std::string format;
    std::size_t audio_size = 0;
};

/// Which modal file picker (if any) is open.
enum class BrowserMode { None, Files, Folder, OutputDir };

/// All mutable UI and job state. Lives on the main thread; the worker thread
/// only touches members guarded by `mtx` and the atomics.
struct App {
    std::vector<std::string> inputs;
    std::string output_dir;
    bool write_cover = true;
    bool recurse = true;

    std::mutex mtx;
    std::vector<Job> jobs;
    std::vector<std::string> pending_drop;
    std::atomic<bool> running{false};
    std::atomic<int> done{0};
    std::atomic<uint64_t> cur_done{0};
    std::atomic<uint64_t> cur_total{0};
    int total = 0;
    std::string current;
    std::chrono::steady_clock::time_point current_since{};
    float bar_shown = 0.0f;  // UI-only: smoothed value of the per-file bar
    std::thread worker;

    std::string status;

    Player player;  // bottom player bar (replays the decrypted output file)
    Cover cover;    // one-slot cover thumbnail for the track being played

    BrowserMode browser_mode = BrowserMode::None;
    std::string browser_dir;
    std::string browser_selected;
    bool browser_popup_open = false;
};

/// miniaudio decodes these; anything else (m4a / aac / ogg) cannot be previewed.
inline bool playable(const std::string& fmt) {
    return fmt == "mp3" || fmt == "flac" || fmt == "wav";
}

/// Add `p` to the input list unless it is already present.
void add_input(App& app, const std::string& p);

/// Expand the inputs and decrypt them on a background thread.
void start_jobs(App& app);

/// File-picker modal.
void open_browser(App& app, BrowserMode m);
void draw_browser(App& app);

/// Bottom transport + visualizer (drawn from `jobs`, in table order).
void draw_player_bar(App& app, const std::vector<Job>& jobs);

} // namespace rencm::app
