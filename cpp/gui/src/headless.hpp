#pragma once

namespace rencm::app {

/// Headless mode: `rencm-gui <file-or-dir> [output-dir]`.
int run_headless(int argc, char** argv);

/// Player self-test: `rencm-gui --playtest <audio-file>`.
///
/// Loads the file, checks the reported duration, plays for a moment, seeks and
/// reports the resulting position. Exits 0 when playback actually advanced.
int run_playtest(const char* path);

/// Cover self-test: `rencm-gui --covercheck <image.jpg>`.
///
/// Decodes the cover image - JPEG or PNG (no GL context needed) - and reports
/// its size plus a hash of
/// the pixels, so a successful decode can be told apart from a blank image.
int run_covercheck(const char* path);

} // namespace rencm::app
