#pragma once

#include <string>

namespace rencm::app {

/// GUI mode. With `autotest` the window runs one decrypt (then a second one,
/// to exercise worker-thread reuse) and exits automatically.
int run_gui(bool autotest = false, std::string autofile = {});

/// Headless self-check of the bundled font: prints which glyphs are available.
int run_fonttest();

} // namespace rencm::app
