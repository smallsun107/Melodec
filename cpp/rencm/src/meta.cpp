#include "meta.hpp"

#include <cctype>
#include <cstdlib>

namespace rencm::internal {
namespace {

void skip_ws(const std::string& s, std::size_t& p) {
    while (p < s.size() && std::isspace((unsigned char)s[p])) ++p;
}

// Returns the position just past the `:` following `"key"`, or npos.
std::size_t find_key(const std::string& s, const char* key) {
    const std::string pat = std::string("\"") + key + "\"";
    std::size_t p = s.find(pat);
    if (p == std::string::npos) return p;
    p += pat.size();
    skip_ws(s, p);
    if (p < s.size() && s[p] == ':') ++p;
    return p;
}

bool read_string(const std::string& s, std::size_t& p, std::string& out) {
    skip_ws(s, p);
    if (p >= s.size() || s[p] != '"') return false;
    ++p;
    std::string r;
    while (p < s.size() && s[p] != '"') {
        if (s[p] == '\\' && p + 1 < s.size()) {
            const char c = s[p + 1];
            p += 2;
            switch (c) {
                case 'n': r.push_back('\n'); break;
                case 't': r.push_back('\t'); break;
                case 'r': r.push_back('\r'); break;
                case '"': r.push_back('"'); break;
                case '\\': r.push_back('\\'); break;
                case '/': r.push_back('/'); break;
                case 'u': {
                    if (p + 4 <= s.size()) {
                        unsigned cp = 0;
                        for (int i = 0; i < 4; ++i) {
                            const char h = s[p + i];
                            cp <<= 4;
                            if (h >= '0' && h <= '9') cp |= (unsigned)(h - '0');
                            else if (h >= 'a' && h <= 'f') cp |= (unsigned)(h - 'a' + 10);
                            else if (h >= 'A' && h <= 'F') cp |= (unsigned)(h - 'A' + 10);
                        }
                        p += 4;
                        if (cp < 0x80) {
                            r.push_back((char)cp);
                        } else if (cp < 0x800) {
                            r.push_back((char)(0xC0 | (cp >> 6)));
                            r.push_back((char)(0x80 | (cp & 0x3F)));
                        } else {
                            r.push_back((char)(0xE0 | (cp >> 12)));
                            r.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
                            r.push_back((char)(0x80 | (cp & 0x3F)));
                        }
                    }
                    break;
                }
                default: r.push_back(c); break;
            }
        } else {
            r.push_back(s[p++]);
        }
    }
    if (p < s.size()) ++p;  // closing quote
    out = r;
    return true;
}

bool read_number(const std::string& s, std::size_t& p, int64_t& out) {
    skip_ws(s, p);
    const std::size_t start = p;
    while (p < s.size() && (std::isdigit((unsigned char)s[p]) || s[p] == '-')) ++p;
    if (p == start) return false;
    out = std::strtoll(s.c_str() + start, nullptr, 10);
    return true;
}

// Accept a number or a quoted numeric string.
bool read_int_or_string(const std::string& s, std::size_t& p, int64_t& out) {
    skip_ws(s, p);
    if (p < s.size() && s[p] == '"') {
        std::string v;
        if (!read_string(s, p, v)) return false;
        out = std::strtoll(v.c_str(), nullptr, 10);
        return true;
    }
    return read_number(s, p, out);
}

void parse_artists(const std::string& s, Metadata& m) {
    std::size_t p = find_key(s, "artist");
    if (p == std::string::npos) return;
    p = s.find('[', p);
    if (p == std::string::npos) return;
    ++p;
    while (p < s.size()) {
        skip_ws(s, p);
        if (p >= s.size() || s[p] == ']') break;
        if (s[p] != '[') {
            ++p;
            continue;
        }
        ++p;
        Artist a;
        if (!read_string(s, p, a.name)) break;
        skip_ws(s, p);
        if (p < s.size() && s[p] == ',') {
            ++p;
            int64_t id = 0;
            if (read_int_or_string(s, p, id)) {
                a.id = id;
                a.has_id = true;
            }
        }
        m.artists.push_back(a);
        while (p < s.size() && s[p] != ']') ++p;  // end of this pair
        if (p < s.size()) ++p;
        skip_ws(s, p);
        if (p < s.size() && s[p] == ',') ++p;
    }
}

} // namespace

Metadata parse_metadata(const std::string& json) {
    Metadata m;
    m.present = true;
    m.raw_json = json;

    std::size_t p;
    std::string sv;
    if ((p = find_key(json, "musicName")) != std::string::npos && read_string(json, p, sv))
        m.music_name = sv;
    if ((p = find_key(json, "album")) != std::string::npos && read_string(json, p, sv))
        m.album = sv;
    if ((p = find_key(json, "format")) != std::string::npos && read_string(json, p, sv))
        m.format = sv;

    int64_t n;
    if ((p = find_key(json, "musicId")) != std::string::npos && read_int_or_string(json, p, n))
        m.music_id = n;
    if ((p = find_key(json, "albumId")) != std::string::npos && read_int_or_string(json, p, n))
        m.album_id = n;
    if ((p = find_key(json, "bitrate")) != std::string::npos && read_number(json, p, n))
        m.bitrate = (int)n;
    if ((p = find_key(json, "duration")) != std::string::npos && read_number(json, p, n))
        m.duration_ms = n;

    parse_artists(json, m);
    return m;
}

} // namespace rencm::internal
