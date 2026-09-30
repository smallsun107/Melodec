#pragma once

#include <cstddef>
#include <cstdint>

namespace rencm::app {

/// Number of fonts linked into the binary. Index 0 is the primary face; the
/// rest are merged into the same ImFont as fallbacks (see CMakeLists.txt).
std::size_t embedded_font_count();

/// Decompressed bytes of font `index` (valid for the process lifetime).
const uint8_t* embedded_font_data(std::size_t index);
std::size_t embedded_font_size(std::size_t index);

/// Total bytes actually stored in the binary (gzip-compressed when zlib is used).
std::size_t embedded_font_stored_size();

} // namespace rencm::app
