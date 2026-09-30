#pragma once

// Internal: audio container sniffing.

#include <cstddef>
#include <cstdint>

#include "rencm/rencm.hpp"

namespace rencm::internal {

/// Sniff the audio container from its first bytes.
Format detect_format(const uint8_t* data, std::size_t size);

} // namespace rencm::internal
