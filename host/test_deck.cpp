#include <cmath>
#include <cstdio>
#include <vector>

#include "deck.hpp"
#include "rk/surface.hpp"

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

static Track song(const char* title, bool playing, uint32_t id) {
    Track t;
    t.known = true, t.playing = playing, t.id = id;
    std::snprintf(t.title, sizeof t.title, "%s", title);
    std::snprintf(t.artist, sizeof t.artist, "ARTIST");
    return t;
}

static void run(Deck& d, float seconds, float dt = 1.0f / 30.0f) {
    for (int i = 0; i < int(seconds / dt); ++i) d.update(dt);
}

static void test_bars_dance_while_playing() {
    Deck d;
    d.set_track(song("A", true, 1));
    run(d, 2.0f);
    int lo = 99, hi = -1;
    bool differ = false;
    for (int i = 0; i < 300; ++i) {
        d.update(1.0f / 30.0f);
        for (int b = 0; b < kBars; ++b) {
            const int v = d.bar(b);
            if (v < 1 || v > kBarSteps) {
                CHECK(false, "bar %d at %d is outside 1..%d", b, v, kBarSteps);
                return;
            }
            lo = v < lo ? v : lo;
            hi = v > hi ? v : hi;
        }
        differ = differ || d.bar(0) != d.bar(1) || d.bar(2) != d.bar(4);
    }
    CHECK(hi - lo >= 4, "bars move over a good range, %d to %d", lo, hi);
    CHECK(differ, "the bars do not all move together");
}

static void test_bars_settle_when_paused() {
    Deck d;
    d.set_track(song("A", true, 1));
    run(d, 2.0f);
    d.set_track(song("A", false, 1));
    run(d, 3.0f);
    for (int b = 0; b < kBars; ++b) CHECK(d.bar(b) == 0, "bar %d is flat after pausing, at %d", b, d.bar(b));
    const uint32_t rev = d.revision();
    run(d, 2.0f);
    CHECK(d.revision() == rev, "settling flat is not news");
}

static void test_bars_ease_in() {
    Deck d;
    d.set_track(song("A", true, 1));
    d.update(1.0f / 60.0f);
    int first = 0;
    for (int b = 0; b < kBars; ++b) first = first > d.bar(b) ? first : d.bar(b);
    CHECK(first <= 4, "the first instant is gentle, the tallest bar is %d", first);
}

static void test_revisions() {
    Deck d;
    d.set_track(song("A", true, 1));
    const uint32_t rev = d.revision();
    d.set_track(song("A", true, 1));
    CHECK(d.revision() == rev, "repeating identical news does not cause a repaint");
    d.set_track(song("A", false, 1));
    CHECK(d.revision() > rev, "pausing needs a repaint for the new status");
    const uint32_t paused = d.revision();
    d.set_track(song("B", false, 2));
    CHECK(d.revision() > paused, "a different song needs a repaint");
    const uint32_t before = d.revision();
    d.set_link(Link::Connecting);
    CHECK(d.revision() == before, "the same link state is not news");
    d.set_link(Link::Offline);
    CHECK(d.revision() > before, "a new link state needs a repaint");
}

static void test_art_arrives_late() {
    Deck d;
    d.set_track(song("A", true, 1));
    CHECK(!d.has_art() && d.accent() == default_accent(), "no art until it arrives, and the house colour meanwhile");
    std::vector<uint16_t> art(kArt * kArt, rk::rgb565(200, 40, 40));
    const uint32_t rev = d.revision();
    d.set_art(art.data(), rk::rgb565(250, 60, 60));
    CHECK(d.has_art() && d.art()[0] == art[0] && d.art()[kArt * kArt - 1] == art[0] && d.revision() > rev, "all of the art is stored and a repaint requested");
    CHECK(d.accent() == rk::rgb565(250, 60, 60), "the accent is taken");
    d.set_track(song("B", true, 2));
    CHECK(!d.has_art() && d.accent() == default_accent(), "a different song drops the old song's art and colour straight away");
    d.set_art(art.data(), rk::rgb565(250, 60, 60));
    d.set_art(nullptr, 0);
    CHECK(!d.has_art() && d.accent() == default_accent(), "null art removes it");
}

static void test_text_restarts_for_a_new_song() {
    Deck d;
    d.set_track(song("A", true, 1));
    run(d, 3.0f);
    CHECK(d.seconds() > 2.9f, "seconds count up");
    d.set_track(song("B", true, 2));
    CHECK(d.seconds() == 0.0f, "a new song starts its scrolling text from the beginning");
    run(d, 1.0f);
    d.set_track(song("B", false, 2));
    CHECK(d.seconds() > 0.9f, "pausing the same song does not rewind the text");
}

static void test_marquee() {
    CHECK(marquee_offset(0, 0) == 0 && marquee_offset(99.0f, 0) == 0, "short text never scrolls");
    CHECK(marquee_offset(0.5f, 120) == 0, "it holds still at the start first");
    int most = 0;
    bool reached_end = false, came_back = false;
    int last = 0;
    for (float t = 0; t < 60.0f; t += 0.02f) {
        const int o = marquee_offset(t, 120);
        if (o < 0 || o > 120) {
            CHECK(false, "offset %d outside 0..120 at t=%.2f", o, t);
            return;
        }
        if (o == 120) reached_end = true;
        if (reached_end && o < last) came_back = true;
        most = o > most ? o : most;
        last = o;
    }
    CHECK(reached_end && most == 120, "it scrolls all the way to the end of the text");
    CHECK(came_back, "then scrolls back, and loops");
    CHECK(marquee_offset(1.0f, 120) == marquee_offset(1.0f + 2.0f * (1.6f + 120.0f / 42.0f), 120), "the loop repeats exactly");
}

static void test_accent() {
    std::vector<uint16_t> red(kArt * kArt, rk::rgb565(180, 30, 30));
    const uint16_t a = accent_of(red.data(), int(red.size()));
    const int r = (a >> 11) & 0x1F, g = (a >> 5) & 0x3F, b = a & 0x1F;
    CHECK(r > 25 && g < 12 && b < 12, "a red cover gives a bright red accent: r%d g%d b%d", r, g, b);

    std::vector<uint16_t> grey(kArt * kArt, rk::rgb565(120, 120, 120));
    CHECK(accent_of(grey.data(), int(grey.size())) == default_accent(), "a grey cover falls back to the house blue");
    std::vector<uint16_t> dark(kArt * kArt, rk::rgb565(10, 5, 5));
    CHECK(accent_of(dark.data(), int(dark.size())) == default_accent(), "a near-black cover falls back too");
    CHECK(accent_of(nullptr, 0) == default_accent(), "no pixels falls back");
    CHECK(default_accent() != rk::rgb565(255, 176, 46), "and the house colour is not the old orange");

    std::vector<uint16_t> mixed(kArt * kArt, rk::rgb565(120, 120, 120));
    for (int i = 0; i < 600; ++i) mixed[i] = rk::rgb565(30, 60, 220);
    const uint16_t m = accent_of(mixed.data(), int(mixed.size()));
    CHECK((m & 0x1F) > 25, "a blue patch on grey gives blue: %04x", m);
}

int main() {
    test_bars_dance_while_playing();
    test_bars_settle_when_paused();
    test_bars_ease_in();
    test_revisions();
    test_art_arrives_late();
    test_text_restarts_for_a_new_song();
    test_marquee();
    test_accent();
    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all deck checks passed\n");
    return 0;
}
