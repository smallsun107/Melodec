#pragma once

// Internal: RC4-style KSA and the precomputed NCM keystream table.

#include <cstddef>
#include <cstdint>

namespace rencm::internal {

/// Build the 256-byte state box from the box key (RC4 KSA).
void key_box(const uint8_t* key, std::size_t key_len, uint8_t out[256]);

/// Precompute the 256-byte NCM keystream table.
///
/// The audio cipher is *not* the standard RC4 PRGA. For each byte index `i`
/// (mod 256):
///
///	j   = i + 1
///	k2  = j + box[j]
///	out = box[box[j] + box[k2]]
///
/// Because it only depends on `i mod 256`, the table is cycled over the audio.
void keystream(const uint8_t box[256], uint8_t out[256]);

} // namespace rencm::internal
