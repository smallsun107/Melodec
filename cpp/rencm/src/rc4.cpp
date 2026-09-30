#include "rc4.hpp"

#include <utility>

namespace rencm::internal {

void key_box(const uint8_t* key, std::size_t key_len, uint8_t out[256]) {
    for (int i = 0; i < 256; ++i) out[i] = (uint8_t)i;

    int j = 0;
    std::size_t kp = 0;
    for (int i = 0; i < 256; ++i) {
        j = (j + out[i] + key[kp]) & 0xFF;
        kp = (kp + 1) % key_len;
        std::swap(out[i], out[j]);
    }
}

void keystream(const uint8_t box[256], uint8_t out[256]) {
    for (int i = 0; i < 256; ++i) {
        const uint8_t a = (uint8_t)(i + 1);
        const uint8_t k2 = (uint8_t)(a + box[a]);
        const uint8_t idx = (uint8_t)(box[a] + box[k2]);
        out[i] = box[idx];
    }
}

} // namespace rencm::internal
