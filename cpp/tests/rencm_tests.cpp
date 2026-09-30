// Decoder regression tests.
//
// Two layers:
//   * always on    - pure-function checks (format_ext) and a SHA-256 self-test;
//   * sample gated - a full golden decode of a real .ncm. Point RENCM_SAMPLE at
//     a file and this asserts the documented vectors from the README (audio /
//     cover / box-key SHA-256 plus section sizes). Without it the golden part
//     prints SKIP and the test still passes.
//
//   RENCM_SAMPLE=/path/sample.ncm ctest --test-dir build --output-on-failure

#include <rencm/rencm.hpp>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

// ------------------------------------------------------------------ SHA-256
// Self-contained; verified against the standard "abc" vector below.
struct Sha256 {
    uint32_t h[8];
    uint64_t len = 0;
    uint8_t buf[64];
    size_t n = 0;

    Sha256() {
        static const uint32_t iv[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                       0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
        std::memcpy(h, iv, sizeof h);
    }

    static uint32_t rotr(uint32_t x, int r) { return (x >> r) | (x << (32 - r)); }

    void block(const uint8_t* p) {
        static const uint32_t k[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4,
            0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe,
            0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f,
            0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
            0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
            0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
            0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116,
            0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
            0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
            0xc67178f2};
        uint32_t w[64];
        for (int i = 0; i < 16; i++)
            w[i] = (uint32_t)p[i * 4] << 24 | (uint32_t)p[i * 4 + 1] << 16 |
                   (uint32_t)p[i * 4 + 2] << 8 | (uint32_t)p[i * 4 + 3];
        for (int i = 16; i < 64; i++) {
            uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; i++) {
            uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            uint32_t ch = (e & f) ^ (~e & g);
            uint32_t t1 = hh + s1 + ch + k[i] + w[i];
            uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            uint32_t t2 = s0 + maj;
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d;
        h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }

    void update(const uint8_t* p, size_t l) {
        len += l;
        while (l > 0) {
            size_t take = 64 - n;
            if (take > l) take = l;
            std::memcpy(buf + n, p, take);
            n += take; p += take; l -= take;
            if (n == 64) { block(buf); n = 0; }
        }
    }

    std::string hexdigest() {
        const uint64_t bits = len * 8;
        uint8_t one = 0x80, zero = 0;
        update(&one, 1);
        while (n != 56) update(&zero, 1);
        uint8_t lb[8];
        for (int i = 0; i < 8; i++) lb[i] = (uint8_t)(bits >> (56 - i * 8));
        update(lb, 8);

        static const char* H = "0123456789abcdef";
        std::string s;
        for (int i = 0; i < 8; i++)
            for (int b = 3; b >= 0; b--) {
                uint8_t byte = (uint8_t)(h[i] >> (b * 8));
                s.push_back(H[byte >> 4]);
                s.push_back(H[byte & 0xF]);
            }
        return s;
    }
};

std::string sha256(const std::vector<uint8_t>& d) {
    Sha256 s;
    if (!d.empty()) s.update(d.data(), d.size());
    return s.hexdigest();
}

// --------------------------------------------------------------- test driver
int failures = 0;

void check(bool ok, const std::string& what) {
    if (ok) {
        std::printf("ok   %s\n", what.c_str());
    } else {
        std::printf("FAIL %s\n", what.c_str());
        ++failures;
    }
}

} // namespace

int main() {
    // --- format_ext -------------------------------------------------------
    check(std::strcmp(rencm::format_ext(rencm::Format::Mp3), "mp3") == 0, "format_ext(mp3)");
    check(std::strcmp(rencm::format_ext(rencm::Format::Flac), "flac") == 0, "format_ext(flac)");
    check(std::strcmp(rencm::format_ext(rencm::Format::M4a), "m4a") == 0, "format_ext(m4a)");
    check(std::strcmp(rencm::format_ext(rencm::Format::Ogg), "ogg") == 0, "format_ext(ogg)");
    check(std::strcmp(rencm::format_ext(rencm::Format::Aac), "aac") == 0, "format_ext(aac)");
    check(std::strcmp(rencm::format_ext(rencm::Format::Unknown), "bin") == 0,
          "format_ext(unknown) -> bin");

    // --- SHA-256 self-test (guards the golden hashes below) ---------------
    {
        const char* abc = "abc";
        std::vector<uint8_t> v(abc, abc + 3);
        check(sha256(v) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
              "sha256(\"abc\") FIPS vector");
    }

    // --- golden decode (needs a real sample) ------------------------------
    const char* sample = std::getenv("RENCM_SAMPLE");
    if (!sample) {
        std::printf("SKIP golden decode (set RENCM_SAMPLE to a .ncm file)\n");
    } else {
        rencm::Result r;
        std::string err;
        check(rencm::decode_file(sample, r, err), "decode sample: " + err);
        if (err.empty()) {
            check(r.version == 1, "version == 1");
            check(r.format == rencm::Format::Mp3, "format == mp3");
            check(r.box_key.size() == 112, "box_key == 112 bytes");
            check(r.cover.size() == 38969, "cover == 38969 bytes");
            check(r.audio.size() == 7448076, "audio == 7448076 bytes");
            check(sha256(r.audio) ==
                      "ea30a900af1843433ff24459c0351b7076508d489296245ac1c2c9865c9d0026",
                  "audio sha256");
            check(sha256(r.cover) ==
                      "05a4de5aafc664c9843a7f808e17185d5eff0f7b88969f32af1fa44c8651e71f",
                  "cover sha256");
            check(sha256(r.box_key) ==
                      "7e07a6ecb23fd345ef8fe390998d4496e951edaad52dc06b0c866ba7174ee685",
                  "box_key sha256");
        }
    }

    std::printf(failures == 0 ? "ALL PASS\n" : "FAILED (%d)\n", failures);
    return failures == 0 ? 0 : 1;
}
