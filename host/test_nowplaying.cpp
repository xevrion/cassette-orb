#include <cstdio>
#include <cstring>
#include <string>

#include "nowplaying.hpp"

using namespace cs;

static int failures = 0;

#define CHECK(cond, ...)                                     \
    do {                                                     \
        if (!(cond)) {                                       \
            std::printf("FAIL %s:%d  ", __FILE__, __LINE__); \
            std::printf(__VA_ARGS__);                        \
            std::printf("\n");                               \
            ++failures;                                      \
        }                                                    \
    } while (0)

static bool parse(const char* json, Track& t) { return parse_now_playing(json, std::strlen(json), t); }

static void test_real_response() {
    // Copied from the live endpoint.
    const char* json =
        "{\"isPlaying\":false,\"title\":\"Blue Eyes\",\"artist\":\"Yo Yo Honey Singh\","
        "\"albumArt\":\"https://i.scdn.co/image/ab67616d0000b27346a4f62d210c2cb02158eac7\","
        "\"songUrl\":\"https://open.spotify.com/track/7wW9scVNLYFLsmBb1yAUmS\"}";
    Track t;
    CHECK(parse(json, t), "parses");
    CHECK(t.known && !t.playing, "known and paused");
    CHECK(std::string(t.title) == "BLUE EYES", "title folded: %s", t.title);
    CHECK(std::string(t.artist) == "YO YO HONEY SINGH", "artist folded: %s", t.artist);
    CHECK(std::strstr(t.art_url, "ab67616d0000b273") != nullptr, "art url kept");
    CHECK(t.id != 0, "has an id");
}

static void test_album_only_while_playing() {
    Track t;
    CHECK(parse("{\"isPlaying\":true,\"title\":\"One\",\"artist\":\"X\",\"album\":\"Caf\\u00e9 Del Mar\"}", t), "parses");
    CHECK(std::string(t.album) == "CAFE DEL MAR", "album is folded like the title: %s", t.album);
    CHECK(parse("{\"isPlaying\":false,\"title\":\"One\",\"artist\":\"X\"}", t) && t.album[0] == '\0', "no album field means an empty album");
    CHECK(parse("{\"title\":\"One\",\"artist\":\"X\",\"album\":7}", t) && t.album[0] == '\0', "a non-string album is ignored");
}

static void test_playing_and_ids() {
    Track a, b, c;
    CHECK(parse("{\"isPlaying\":true,\"title\":\"One\",\"artist\":\"X\"}", a) && a.playing, "playing flag");
    CHECK(parse("{\"isPlaying\":false,\"title\":\"One\",\"artist\":\"X\"}", b), "parses");
    CHECK(a.id == b.id, "same song, same id whether or not it is playing");
    CHECK(parse("{\"title\":\"Two\",\"artist\":\"X\"}", c) && c.id != a.id, "a different song has a different id");
    Track d;
    CHECK(parse("{\"title\":\"One\",\"artist\":\"Y\"}", d) && d.id != a.id, "same title by another artist differs");
}

static void test_not_authenticated() {
    Track t;
    t.playing = true;
    CHECK(parse("{\"isPlaying\":false,\"message\":\"Not authenticated\"}", t), "a valid object without a song");
    CHECK(!t.known && !t.playing && t.title[0] == '\0', "no song means unknown");
    CHECK(parse("{\"isPlaying\":false,\"error\":\"Failed to fetch playback\"}", t) && !t.known, "an error object is not a song");
}

static void test_escapes_and_unicode() {
    Track t;
    CHECK(parse("{\"title\":\"Say \\\"Hi\\\" \\\\ now\",\"artist\":\"A\"}", t), "parses");
    CHECK(std::string(t.title) == "SAY \"HI\" \\ NOW" || std::string(t.title) == "SAY \"HI\" ? NOW", "escaped quote survives: %s", t.title);
    CHECK(parse("{\"title\":\"Beyonc\\u00e9\",\"artist\":\"A\"}", t) && std::string(t.title) == "BEYONCE", "\\u00e9 folds to E: %s", t.title);
    CHECK(parse("{\"title\":\"Raw \xC3\xA9\",\"artist\":\"A\"}", t) && std::string(t.title) == "RAW E", "raw utf-8 folds: %s", t.title);
    CHECK(parse("{\"title\":\"Hi \\ud83c\\udfb5\",\"artist\":\"A\"}", t) && std::string(t.title) == "HI ?", "a surrogate pair is one emoji, one '?': %s", t.title);
    CHECK(parse("{\"title\":\"Lone \\ud83c x\",\"artist\":\"A\"}", t), "a lone surrogate does not break parsing");
}

static void test_nested_and_noise() {
    Track t;
    // A nested object holding a "title" must not be mistaken for the track title.
    const char* json = "{\"meta\":{\"title\":\"WRONG\",\"list\":[1,2,{\"title\":\"ALSO WRONG\"}]},\"title\":\"Right\",\"artist\":\"A\",\"extra\":[\"x,}\",true,null,12.5]}";
    CHECK(parse(json, t), "parses");
    CHECK(std::string(t.title) == "RIGHT", "top-level title wins: %s", t.title);
    CHECK(parse("  {  \"title\" :  \"Spaced\" ,\n \"artist\" : \"A\" , }  ", t) && std::string(t.title) == "SPACED", "whitespace and a trailing comma are tolerated");
    CHECK(parse("{\"title\":123,\"artist\":\"A\"}", t) && !t.known, "a non-string title is ignored, not misread");
    CHECK(parse("{\"isPlaying\":\"yes\",\"title\":\"T\"}", t) && !t.playing, "a non-boolean isPlaying counts as not playing");
}

static void test_garbage_in() {
    Track t;
    t.playing = true;
    std::strcpy(t.title, "KEEP");
    CHECK(!parse("", t), "empty");
    CHECK(!parse("<html><body>502 Bad Gateway</body></html>", t), "an html error page");
    CHECK(!parse("{\"title\":\"cut off", t), "truncated string");
    CHECK(!parse("{\"title\":", t), "truncated after colon");
    CHECK(!parse("{\"title\" \"x\"}", t), "missing colon");
    CHECK(!parse("{\"title\":\"x\" \"artist\":\"y\"}", t), "missing comma");
    CHECK(!parse("{\"a\":{\"b\":[1,2}", t), "unbalanced nesting");
    CHECK(!parse("[1,2,3]", t), "an array is not our object");
    CHECK(t.playing && std::string(t.title) == "KEEP", "a failed parse leaves the previous track alone");
}

static void test_long_values_truncate() {
    std::string json = "{\"title\":\"";
    json.append(5000, 'a');
    json += "\",\"artist\":\"";
    json.append(5000, 'b');
    json += "\"}";
    Track t;
    CHECK(parse_now_playing(json.data(), json.size(), t), "parses a huge response");
    CHECK(std::strlen(t.title) < sizeof t.title && std::strlen(t.title) > 50, "title truncated to fit, %zu chars", std::strlen(t.title));
    CHECK(std::strlen(t.artist) < sizeof t.artist, "artist truncated to fit");
}

static void test_binary_junk_does_not_crash() {
    unsigned seed = 12345;
    for (int round = 0; round < 2000; ++round) {
        char buf[200];
        int n = 1 + int(seed % 190);
        for (int i = 0; i < n; ++i) {
            seed = seed * 1103515245u + 12345u;
            // Mostly JSON punctuation, so the parser gets pushed down its odd paths.
            static const char alphabet[] = "{}[]\":,\\ ntu0123456789abcdefABCDEFtruefalsetitleartist";
            buf[i] = alphabet[(seed >> 16) % (sizeof alphabet - 1)];
        }
        Track t;
        parse_now_playing(buf, size_t(n), t);
    }
    CHECK(true, "survived junk");
}

static void test_art_urls() {
    char out[200];
    const char* big = "https://i.scdn.co/image/ab67616d0000b27346a4f62d210c2cb02158eac7";
    CHECK(small_art_url(big, out, sizeof out, 64) && std::string(out) == "https://i.scdn.co/image/ab67616d0000485146a4f62d210c2cb02158eac7", "64px: %s", out);
    CHECK(small_art_url(big, out, sizeof out, 300) && std::string(out) == "https://i.scdn.co/image/ab67616d00001e0246a4f62d210c2cb02158eac7", "300px: %s", out);
    CHECK(small_art_url(big, out, sizeof out) && std::string(out).find("ab67616d00001e02") != std::string::npos, "the default is the 300px cover");
    CHECK(small_art_url("https://i.scdn.co/image/ab67616d00004851deadbeef", out, sizeof out), "an already-small url is accepted");
    CHECK(!small_art_url("https://example.com/cover.jpg", out, sizeof out), "an unknown url is refused");
    CHECK(!small_art_url("", out, sizeof out), "empty url is refused");
    CHECK(!small_art_url(big, out, 10), "too small a buffer is refused, not overrun");
    CHECK(!small_art_url(big, out, sizeof out, 128), "an unsupported size is refused");
    CHECK(!small_art_url("https://i.scdn.co/image/ab67616d0000", out, sizeof out), "a url cut off at the size code is refused");
}

int main() {
    test_real_response();
    test_album_only_while_playing();
    test_playing_and_ids();
    test_not_authenticated();
    test_escapes_and_unicode();
    test_nested_and_noise();
    test_garbage_in();
    test_long_values_truncate();
    test_binary_junk_does_not_crash();
    test_art_urls();
    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all now-playing checks passed\n");
    return 0;
}
