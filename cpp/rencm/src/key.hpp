#pragma once

// Internal: decryption of the two NCM container sections.

#include <cstdint>
#include <string>
#include <vector>

namespace rencm::internal {

/// Derive the box key from the raw key section:
/// XOR 0x64 -> AES-128-ECB(core key) -> PKCS#7 unpad -> strip "neteasecloudmusic".
bool decrypt_key(const std::vector<uint8_t>& blob, std::vector<uint8_t>& out, std::string& err);

/// Decode the raw comment section into the metadata JSON (without `music:`):
/// XOR 0x63 -> strip marker -> base64 -> AES-128-ECB(meta key) -> strip "music:".
bool decrypt_comment(const std::vector<uint8_t>& comment, std::string& json, std::string& err);

} // namespace rencm::internal
