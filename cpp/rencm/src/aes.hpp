#pragma once

// Internal: AES-128-ECB decryption (self-contained, no third-party code).

#include <cstddef>
#include <cstdint>

namespace rencm::internal {

/// Decrypt `len` bytes (a multiple of 16) with AES-128-ECB. `in` and `out` may alias.
bool aes_ecb_decrypt(const uint8_t key[16], const uint8_t* in, uint8_t* out, std::size_t len);

} // namespace rencm::internal
