#include "key.hpp"

#include "aes.hpp"

#include <cstring>
#include <utility>

namespace rencm::internal {
namespace {

constexpr const char* kKeyPrefix = "neteasecloudmusic";
constexpr const char* kMetaMarker = "163 key(Don't modify):";
constexpr const char* kMetaPrefix = "music:";
constexpr uint8_t kKeyXor = 0x64;
constexpr uint8_t kMetaXor = 0x63;

const uint8_t kCoreKey[16] = {'h','z','H','R','A','m','s','o','5','k','I','n','b','a','x','W'};
const uint8_t kMetaKey[16] = {'#','1','4','l','j','k','_','!','\\',']','&','0','U','<','\'','('};

std::vector<uint8_t> xor_all(const std::vector<uint8_t>& d, uint8_t k) {
    std::vector<uint8_t> o(d.size());
    for (std::size_t i = 0; i < d.size(); ++i) o[i] = (uint8_t)(d[i] ^ k);
    return o;
}

std::vector<uint8_t> pkcs7_unpad(std::vector<uint8_t> d) {
    if (d.empty()) return d;
    const std::size_t pad = d.back();
    if (pad >= 1 && pad <= 16 && pad <= d.size()) {
        bool ok = true;
        for (std::size_t i = d.size() - pad; i < d.size(); ++i)
            if (d[i] != pad) {
                ok = false;
                break;
            }
        if (ok) d.resize(d.size() - pad);
    }
    return d;
}

// AES-128-ECB decrypt of whole 16-byte blocks.
std::vector<uint8_t> aes_decrypt_blob(std::vector<uint8_t> d, const uint8_t key[16]) {
    d.resize(d.size() - d.size() % 16);
    if (!d.empty()) aes_ecb_decrypt(key, d.data(), d.data(), d.size());
    return d;
}

std::string base64_decode(const std::string& in) {
    auto val = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };
    std::string out;
    int buf = 0, bits = 0;
    for (char c : in) {
        const int v = val(c);
        if (v < 0) continue;
        // Mask to 24 bits: only the low `bits` bits are ever read back, and an
        // unmasked accumulator would signed-overflow (UB) after ~5 characters.
        buf = ((buf << 6) | v) & 0xFFFFFF;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back((char)((buf >> bits) & 0xFF));
        }
    }
    return out;
}

} // namespace

bool decrypt_key(const std::vector<uint8_t>& blob, std::vector<uint8_t>& out, std::string& err) {
    std::vector<uint8_t> plain = pkcs7_unpad(aes_decrypt_blob(xor_all(blob, kKeyXor), kCoreKey));
    const std::size_t plen = std::strlen(kKeyPrefix);
    if (plain.size() <= plen || std::memcmp(plain.data(), kKeyPrefix, plen) != 0) {
        err = "key validation failed";
        return false;
    }
    out.assign(plain.begin() + plen, plain.end());
    if (out.empty()) {
        err = "empty box key";
        return false;
    }
    return true;
}

bool decrypt_comment(const std::vector<uint8_t>& comment, std::string& json_out, std::string& err) {
    const std::vector<uint8_t> c = xor_all(comment, kMetaXor);
    const std::size_t mk = std::strlen(kMetaMarker);
    if (c.size() <= mk || std::memcmp(c.data(), kMetaMarker, mk) != 0) {
        err = "invalid comment section";
        return false;
    }

    std::string b64;
    for (std::size_t i = mk; i < c.size() && c[i] != 0; ++i) b64.push_back((char)c[i]);
    while (b64.size() % 4) b64.push_back('=');

    const std::string decoded = base64_decode(b64);
    std::vector<uint8_t> bytes(decoded.begin(), decoded.end());
    std::vector<uint8_t> plain = pkcs7_unpad(aes_decrypt_blob(std::move(bytes), kMetaKey));

    const std::size_t mp = std::strlen(kMetaPrefix);
    if (plain.size() <= mp || std::memcmp(plain.data(), kMetaPrefix, mp) != 0) {
        err = "invalid metadata section";
        return false;
    }
    json_out.assign((const char*)plain.data() + mp, plain.size() - mp);
    return true;
}

} // namespace rencm::internal
