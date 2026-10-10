#include "nowplaying.hpp"

#include <cstring>

#include "rk/font.hpp"

namespace cs {

namespace {

class Reader {
public:
    Reader(const char* p, size_t n) : p_(p), end_(p + n) {}

    void skip_ws() {
        while (p_ < end_ && (*p_ == ' ' || *p_ == '\t' || *p_ == '\n' || *p_ == '\r')) ++p_;
    }
    bool eof() const { return p_ >= end_; }
    char peek() const { return p_ < end_ ? *p_ : '\0'; }
    bool eat(char c) {
        skip_ws();
        if (p_ < end_ && *p_ == c) {
            ++p_;
            return true;
        }
        return false;
    }

    // Reads a JSON string into out as UTF-8, truncating if it does not fit.
    bool string(char* out, size_t cap) {
        skip_ws();
        if (p_ >= end_ || *p_ != '"') return false;
        ++p_;
        size_t n = 0;
        auto put = [&](char c) {
            if (cap && n + 1 < cap) out[n++] = c;
        };
        auto put_cp = [&](uint32_t cp) {
            if (cp < 0x80) {
                put(char(cp));
            } else if (cp < 0x800) {
                put(char(0xC0 | (cp >> 6))), put(char(0x80 | (cp & 0x3F)));
            } else if (cp < 0x10000) {
                put(char(0xE0 | (cp >> 12))), put(char(0x80 | ((cp >> 6) & 0x3F))), put(char(0x80 | (cp & 0x3F)));
            } else {
                put(char(0xF0 | (cp >> 18))), put(char(0x80 | ((cp >> 12) & 0x3F)));
                put(char(0x80 | ((cp >> 6) & 0x3F))), put(char(0x80 | (cp & 0x3F)));
            }
        };
        while (p_ < end_ && *p_ != '"') {
            char c = *p_++;
            if (c != '\\') {
                put(c);
                continue;
            }
            if (p_ >= end_) return false;
            const char e = *p_++;
            switch (e) {
            case 'n': put('\n'); break;
            case 't': put('\t'); break;
            case 'r': put('\r'); break;
            case 'b': case 'f': put(' '); break;
            case 'u': {
                uint32_t cp = 0;
                if (!hex4(cp)) return false;
                // A high surrogate must be followed by \uDC00..DFFF to make one character.
                if (cp >= 0xD800 && cp < 0xDC00 && p_ + 1 < end_ && p_[0] == '\\' && p_[1] == 'u') {
                    p_ += 2;
                    uint32_t lo = 0;
                    if (!hex4(lo)) return false;
                    cp = lo >= 0xDC00 && lo < 0xE000 ? 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00) : 0xFFFD;
                } else if (cp >= 0xD800 && cp < 0xE000) {
                    cp = 0xFFFD;
                }
                put_cp(cp);
                break;
            }
            default: put(e); break;  // \" \\ \/ and anything unexpected
            }
        }
        if (p_ >= end_) return false;
        ++p_;
        if (cap) out[n] = '\0';
        return true;
    }

    // Skips any value, including nested objects and arrays.
    bool skip_value() {
        skip_ws();
        if (p_ >= end_) return false;
        if (*p_ == '"') return string(nullptr, 0);
        if (*p_ == '{' || *p_ == '[') {
            int depth = 0;
            while (p_ < end_) {
                const char c = *p_;
                if (c == '"') {
                    if (!string(nullptr, 0)) return false;
                    continue;
                }
                ++p_;
                if (c == '{' || c == '[') ++depth;
                if (c == '}' || c == ']') {
                    if (--depth == 0) return true;
                }
            }
            return false;
        }
        const char* start = p_;
        while (p_ < end_ && *p_ != ',' && *p_ != '}' && *p_ != ']' && *p_ != ' ' && *p_ != '\n' && *p_ != '\r' && *p_ != '\t') ++p_;
        return p_ > start;
    }

    // true / false, anything else is false.
    bool boolean(bool& v) {
        skip_ws();
        if (end_ - p_ >= 4 && std::strncmp(p_, "true", 4) == 0) {
            p_ += 4;
            v = true;
            return true;
        }
        if (end_ - p_ >= 5 && std::strncmp(p_, "false", 5) == 0) {
            p_ += 5;
            v = false;
            return true;
        }
        return false;
    }

private:
    bool hex4(uint32_t& out) {
        if (end_ - p_ < 4) return false;
        out = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = *p_++;
            out <<= 4;
            if (c >= '0' && c <= '9') out |= uint32_t(c - '0');
            else if (c >= 'a' && c <= 'f') out |= uint32_t(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') out |= uint32_t(c - 'A' + 10);
            else return false;
        }
        return true;
    }

    const char* p_;
    const char* end_;
};

uint32_t fnv1a(const char* s, uint32_t h = 2166136261u) {
    for (; *s; ++s) h = (h ^ static_cast<unsigned char>(*s)) * 16777619u;
    return h;
}

}  // namespace

bool parse_now_playing(const char* json, size_t len, Track& out) {
    Reader r(json, len);
    if (!r.eat('{')) return false;

    Track t;
    char raw_title[256] = "", raw_artist[256] = "", raw_album[256] = "";
    bool first = true;
    for (;;) {
        r.skip_ws();
        if (r.eat('}')) break;
        if (!first && !r.eat(',')) return false;
        first = false;
        r.skip_ws();
        if (r.eat('}')) break;  // tolerate a trailing comma

        char key[32] = "";
        if (!r.string(key, sizeof key) || !r.eat(':')) return false;

        bool ok = true;
        r.skip_ws();
        const bool is_string = r.peek() == '"';
        if (std::strcmp(key, "isPlaying") == 0) {
            bool v = false;
            ok = r.boolean(v);
            t.playing = ok && v;
            if (!ok) ok = r.skip_value();
        } else if (is_string && std::strcmp(key, "title") == 0) ok = r.string(raw_title, sizeof raw_title);
        else if (is_string && std::strcmp(key, "artist") == 0) ok = r.string(raw_artist, sizeof raw_artist);
        else if (is_string && std::strcmp(key, "album") == 0) ok = r.string(raw_album, sizeof raw_album);
        else if (is_string && std::strcmp(key, "albumArt") == 0) ok = r.string(t.art_url, sizeof t.art_url);
        else ok = r.skip_value();
        if (!ok) return false;
    }

    rk::fold_text(raw_title, t.title, sizeof t.title);
    rk::fold_text(raw_artist, t.artist, sizeof t.artist);
    rk::fold_text(raw_album, t.album, sizeof t.album);
    t.known = t.title[0] != '\0';
    t.id = t.known ? fnv1a(t.artist, fnv1a(t.title)) : 0;
    out = t;
    return true;
}

bool small_art_url(const char* url, char* out, size_t cap, int size) {
    // Spotify names the same cover at several sizes by one 4-character code after "ab67616d0000".
    static const char kPrefix[] = "ab67616d0000";
    const char* code = nullptr;
    if (size == 64) code = "4851";
    else if (size == 300) code = "1e02";
    if (!code || !cap) return false;

    const char* at = std::strstr(url, kPrefix);
    if (!at) return false;
    const size_t head = size_t(at - url) + sizeof(kPrefix) - 1;
    if (std::strlen(url) < head + 4) return false;
    const size_t n = std::strlen(url);
    if (n + 1 > cap) return false;
    std::memcpy(out, url, n + 1);
    std::memcpy(out + head, code, 4);
    return true;
}

}  // namespace cs
