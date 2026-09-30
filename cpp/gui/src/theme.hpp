#pragma once

// Rose Pine theme helpers.

#include "imgui.h"

namespace rencm::app {

/// Rose Pine colour from a 0xRRGGBB literal.
ImVec4 rp(unsigned rgb, float a = 1.0f);

/// Apply the Rose Pine theme to the current ImGui style.
void apply_rose_pine();

} // namespace rencm::app
