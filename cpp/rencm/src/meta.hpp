#pragma once

// Internal: metadata JSON extraction (dependency-free, no JSON library).

#include <string>

#include "rencm/rencm.hpp"

namespace rencm::internal {

/// Extract the fields we care about from the `music:{...}` payload.
Metadata parse_metadata(const std::string& json);

} // namespace rencm::internal
