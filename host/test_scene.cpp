#include <cstdio>
#include <algorithm>
#include <cstring>
#include <vector>

#include "deck.hpp"
#include "scene.hpp"

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

constexpr float kDt = 1.0f / 30.0f;
constexpr int kCoverX = 233 - kArt / 2, kCoverY = 52;  // as in the scene

static Track song(const char* title, const char* artist, const char* album, bool playing, uint32_t id) {
    Track t;
    t.known = true, t.playing = playing, t.id = id;
    std::snprintf(t.title, sizeof t.title, "%s", title);
    std::snprintf(t.artist, sizeof t.artist, "%s", artist);
    std::snprintf(t.album, sizeof t.album, "%s", album);
    return t;
}

static std::vector<uint16_t> fake_art(int seed) {
    std::vector<uint16_t> px(kArt * kArt);
    for (int y = 0; y < kArt; ++y)
        for (int x = 0; x < kArt; ++x) px[y * kArt + x] = rk::rgb565(uint8_t(x + seed * 30), uint8_t(y), uint8_t(255 - x / 2 - y / 2 + seed * 7));
    return px;
}

// Four coloured quadrants: top left red, top right green, bottom left blue, bottom right yellow.
static std::vector<uint16_t> quadrant_art() {
    std::vector<uint16_t> px(kArt * kArt);
    for (int y = 0; y < kArt; ++y)
        for (int x = 0; x < kArt; ++x) {
            const bool right = x >= kArt / 2, bottom = y >= kArt / 2;
            px[y * kArt + x] = !right && !bottom ? rk::rgb565(240, 20, 20) : right && !bottom ? rk::rgb565(20, 240, 20) : !right && bottom ? rk::rgb565(20, 20, 240) : rk::rgb565(240, 240, 20);
        }
    return px;
}

static int differing(const std::vector<uint16_t>& a, const std::vector<uint16_t>& b, int* first) {
    int n = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i] == b[i]) continue;
        if (!n) *first = int(i);
        ++n;
    }
    return n;
}

static long area(const rk::Rect& r) { return long(r.x1 - r.x0 + 1) * long(r.y1 - r.y0 + 1); }

// Total pixels the panel would be sent for this frame, with the boxes counted separately.
static long frame_cost(rk::Surface& sf, int* boxes = nullptr) {
    rk::Rect r[rk::Surface::kMaxDirty];
    const int n = sf.take_dirty_rects(r, rk::Surface::kMaxDirty);
    long total = 0;
    for (int i = 0; i < n; ++i) total += area(r[i]);
    if (boxes) *boxes = n;
    return total;
}

enum class Hue { Red, Green, Blue, Yellow, Other };

static Hue hue_at(const std::vector<uint16_t>& px, int x, int y) {
    const uint16_t p = px[y * kScreen + x];
    const int r8 = ((p >> 11) & 0x1F) * 255 / 31, g8 = ((p >> 5) & 0x3F) * 255 / 63, b8 = (p & 0x1F) * 255 / 31;
    if (r8 > 150 && g8 < 90 && b8 < 90) return Hue::Red;
    if (g8 > 150 && r8 < 90 && b8 < 90) return Hue::Green;
    if (b8 > 150 && r8 < 90 && g8 < 90) return Hue::Blue;
    if (r8 > 150 && g8 > 150 && b8 < 90) return Hue::Yellow;
    return Hue::Other;
}

static int brightness(const std::vector<uint16_t>& px, int x, int y) {
    const uint16_t p = px[y * kScreen + x];
    return ((p >> 11) & 0x1F) * 255 / 31 + ((p >> 5) & 0x3F) * 255 / 63 + (p & 0x1F) * 255 / 31;
}

static std::vector<uint16_t> paint(Deck& deck, float settle_seconds = 1.0f) {
    for (float t = 0; t < settle_seconds; t += kDt) deck.update(kDt);
    std::vector<uint16_t> px(kScreen * kScreen, 0);
    rk::Surface sf(reinterpret_cast<uint8_t*>(px.data()), kScreen, kScreen, kScreen * 2);
    Scene scene;
    scene.draw(sf, deck);
    return px;
}

// The incremental painter must always end up with the picture a fresh paint gives. A stale
// pixel here is the "line left on screen" kind of bug.
static void test_incremental_matches_fresh() {
    Deck deck;
    std::vector<uint16_t> live(kScreen * kScreen, 0), fresh(kScreen * kScreen, 0);
    rk::Surface live_sf(reinterpret_cast<uint8_t*>(live.data()), kScreen, kScreen, kScreen * 2);
    Scene scene;
    int compared = 0, bad_frames = 0;

    const auto art1 = fake_art(1), art2 = fake_art(2);
    const char* long_title = "A VERY LONG TITLE THAT WILL HAVE TO SCROLL ACROSS THE SCREEN";
    const char* long_artist = "AN EQUALLY LONG ARTIST NAME THAT SCROLLS TOO";
    const char* long_album = "AND AN ALBUM NAME THAT IS JUST AS LONG AS THE OTHERS";
    for (int frame = 0; frame < 1800; ++frame) {
        // A script that touches every state: connecting, nothing playing, a short song, a song
        // whose three lines all scroll, paused, an accented song, offline and back.
        if (frame == 0) deck.set_link(Link::Connecting);
        if (frame == 20) deck.set_link(Link::Online);
        if (frame == 40) deck.set_track(song("SHORT", "ART", "", true, 1));
        if (frame == 70) deck.set_art(art1.data(), accent_of(art1.data(), kArt * kArt));
        if (frame == 400) deck.set_track(song(long_title, long_artist, long_album, true, 2));
        if (frame == 430) deck.set_art(art2.data(), accent_of(art2.data(), kArt * kArt));
        if (frame == 900) deck.set_track(song(long_title, long_artist, long_album, false, 2));
        if (frame == 1100) deck.set_track(song("BEYONCE", "?", "", true, 3));
        if (frame == 1300) deck.set_link(Link::Offline);
        if (frame == 1400) deck.set_link(Link::Error);
        if (frame == 1450) deck.set_link(Link::Online);
        if (frame == 1500) deck.set_track(Track());
        if (frame == 200) deck.set_battery(64, false);
        if (frame == 700) deck.set_battery(60, true);
        if (frame == 1000) deck.set_battery(8, false);
        if (frame == 1600) deck.set_battery(-1, false);
        deck.update(kDt);
        scene.draw(live_sf, deck);

        rk::Rect r[rk::Surface::kMaxDirty];
        const int n = live_sf.take_dirty_rects(r, rk::Surface::kMaxDirty);
        for (int i = 0; i < n; ++i) {
            CHECK((r[i].x0 & 1) == 0 && (r[i].y0 & 1) == 0 && (r[i].x1 & 1) == 1 && (r[i].y1 & 1) == 1, "dirty box %d,%d..%d,%d is not even-aligned", r[i].x0, r[i].y0, r[i].x1, r[i].y1);
            CHECK(r[i].x0 >= 0 && r[i].y0 >= 0 && r[i].x1 < kScreen && r[i].y1 < kScreen, "dirty box out of bounds");
        }

        if (frame % 5 == 0) {
            rk::Surface fresh_sf(reinterpret_cast<uint8_t*>(fresh.data()), kScreen, kScreen, kScreen * 2);
            Scene full;
            full.draw(fresh_sf, deck);
            int first = 0;
            const int bad = differing(live, fresh, &first);
            if (bad && bad_frames++ < 3) CHECK(false, "frame %d: %d pixels differ from a fresh paint, first at %d,%d", frame, bad, first % kScreen, first / kScreen);
            ++compared;
        }
    }
    CHECK(bad_frames == 0, "%d frames differed from a fresh paint", bad_frames);
    CHECK(compared > 300, "compared only %d frames", compared);
}

// The cover is drawn pixel for pixel: not mirrored, not rotated, not shifted.
static void test_cover_is_exact() {
    const auto art = quadrant_art();
    Deck deck;
    deck.set_link(Link::Online);
    deck.set_track(song("QUAD", "ART", "ALBUM", true, 1));
    deck.set_art(art.data(), default_accent());
    const auto px = paint(deck);
    const int d = 70;
    const int cx = kCoverX + kArt / 2, cy = kCoverY + kArt / 2;
    CHECK(hue_at(px, cx - d, cy - d) == Hue::Red, "top left of the tile is the top left of the cover");
    CHECK(hue_at(px, cx + d, cy - d) == Hue::Green, "top right");
    CHECK(hue_at(px, cx - d, cy + d) == Hue::Blue, "bottom left");
    CHECK(hue_at(px, cx + d, cy + d) == Hue::Yellow, "bottom right");
    // Pixel for pixel: a point well inside the cover is exactly the source pixel.
    CHECK(px[(kCoverY + 60) * kScreen + kCoverX + 90] == art[60 * kArt + 90], "an interior pixel is copied exactly");
    CHECK(px[(kCoverY + kArt - 40) * kScreen + kCoverX + 40] == art[(kArt - 40) * kArt + 40], "and so is another");
}

static void test_cover_has_rounded_corners() {
    const auto art = quadrant_art();
    Deck deck;
    deck.set_link(Link::Online);
    deck.set_track(song("QUAD", "ART", "", true, 1));
    deck.set_art(art.data(), default_accent());
    const auto px = paint(deck);
    // The very corner of the cover's square is cut away, and the middle of each edge is not.
    const bool corner_cut = hue_at(px, kCoverX, kCoverY) == Hue::Other && hue_at(px, kCoverX + kArt - 1, kCoverY) == Hue::Other &&
                            hue_at(px, kCoverX, kCoverY + kArt - 1) == Hue::Other && hue_at(px, kCoverX + kArt - 1, kCoverY + kArt - 1) == Hue::Other;
    CHECK(corner_cut, "all four corners of the cover are rounded off");
    CHECK(hue_at(px, kCoverX + kArt / 2 - 40, kCoverY + 1) == Hue::Red || hue_at(px, kCoverX + kArt / 2 + 40, kCoverY + 1) == Hue::Green, "but the edges between the corners are full colour");
    CHECK(hue_at(px, kCoverX + 40, kCoverY + 1) == Hue::Red, "a little in from the corner it is the cover again");
}

static void test_paused_cover_is_dimmer() {
    const auto art = fake_art(1);
    Deck playing, paused;
    for (Deck* d : {&playing, &paused}) {
        d->set_link(Link::Online);
        d->set_track(song("SONG", "ART", "", d == &playing, 1));
        d->set_art(art.data(), default_accent());
    }
    const auto a = paint(playing), b = paint(paused);
    const int x = kCoverX + kArt / 2, y = kCoverY + kArt / 2;
    CHECK(brightness(b, x, y) < brightness(a, x, y) * 3 / 4 && brightness(b, x, y) > 0, "a paused cover is dimmed but still visible: %d against %d", brightness(b, x, y), brightness(a, x, y));
}

static void test_glow_and_ring() {
    const auto art = quadrant_art();
    Deck deck;
    deck.set_link(Link::Online);
    deck.set_track(song("QUAD", "ART", "", true, 1));
    const uint16_t accent = rk::rgb565(250, 60, 120);
    deck.set_art(art.data(), accent);
    const auto px = paint(deck);
    const int mid_y = kCoverY + kArt / 2;
    CHECK(px[mid_y * kScreen + kCoverX - 5] != 0, "a halo sits just outside the cover");
    CHECK(px[mid_y * kScreen + kCoverX - 5] != accent, "and it is a soft tint, not the solid accent");
    CHECK(brightness(px, kCoverX - 5, mid_y) > brightness(px, kCoverX - 24, mid_y), "brighter near the cover than far from it");
    CHECK(px[233 * kScreen + 233 + 226] == accent, "the ring around the screen is the accent colour while playing");
    CHECK(px[2 * kScreen + 233] == 0 || px[2 * kScreen + 233] == accent, "the ring is at the very edge");
}

static void test_each_state_looks_different() {
    const auto art = fake_art(2);
    auto make = [&](Link link, bool known, bool playing) {
        Deck d;
        d.set_link(link);
        if (known) {
            d.set_track(song("SONG", "ART", "ALBUM", playing, 1));
            d.set_art(art.data(), accent_of(art.data(), kArt * kArt));
        }
        return paint(d);
    };
    const auto online_playing = make(Link::Online, true, true), online_paused = make(Link::Online, true, false), nothing = make(Link::Online, false, false);
    const auto connecting = make(Link::Connecting, false, false), offline = make(Link::Offline, false, false), error = make(Link::Error, true, false);
    int first = 0;
    CHECK(differing(online_playing, online_paused, &first) > 1000, "playing and paused look different");
    CHECK(differing(nothing, connecting, &first) > 50, "nothing playing and connecting look different");
    CHECK(differing(connecting, offline, &first) > 50, "connecting and offline look different");
    CHECK(differing(online_paused, error, &first) > 50, "paused and no signal look different");
}

// The Wi-Fi icon tells the four network states apart at a glance.
static void test_wifi_icon() {
    auto icon = [](Link link) {
        Deck d;
        d.set_link(link);
        return paint(d, 0.2f);
    };
    // The top of the outer arc sits straight above the dot, 26 pixels up (dot at y 436).
    const int top = 436 - 26, mid = 436 - 18, dot = 436;
    const auto online = icon(Link::Online), amber = icon(Link::Error), joining = icon(Link::Connecting), down = icon(Link::Offline);
    CHECK(hue_at(online, 233, top) == Hue::Other && ((online[top * kScreen + 233] >> 5) & 0x3F) > 40, "online lights the outer arc green");
    CHECK(online[mid * kScreen + 233] == online[top * kScreen + 233], "all three arcs share the colour when online");
    CHECK(amber[top * kScreen + 233] != online[top * kScreen + 233], "no internet is not drawn in the online colour");
    CHECK(amber[top * kScreen + 233] != down[top * kScreen + 233], "and not in the down colour either");
    CHECK(joining[mid * kScreen + 233] == joining[436 * kScreen + 233] || joining[dot * kScreen + 233] != 0, "joining shows the dot");
    CHECK(joining[top * kScreen + 233] != joining[dot * kScreen + 233], "joining lights only the inner arc, the outer one stays faint");
    CHECK(down[top * kScreen + 233] != down[dot * kScreen + 233], "with no network only the dot is lit, in red");
    CHECK(down[dot * kScreen + 233] != online[dot * kScreen + 233], "red dot against green dot");
    CHECK(brightness(down, 233, top) > 0, "the faint outline is still there");
}

// A battery gauge shows only when a battery is fitted, and its fill follows the percentage.
static void test_battery_gauge() {
    auto gauge = [](int pct, bool charging) {
        Deck d;
        d.set_link(Link::Online);
        d.set_battery(pct, charging);
        return paint(d, 0.2f);
    };
    const auto none = gauge(-1, false), full = gauge(100, false), half = gauge(50, false), low = gauge(10, false), empty = gauge(0, false), plug = gauge(50, true);
    // The cell is at x 268..293, y 424..437; its inside runs from x 271 to 290 at y 430.
    auto at = [](const std::vector<uint16_t>& px, int x) { return px[430 * kScreen + x]; };
    CHECK(at(none, 280) == 0 && at(none, 270) == 0, "no battery, nothing drawn");
    CHECK(at(full, 289) != 0 && at(full, 272) != 0, "a full cell is filled end to end");
    CHECK(at(half, 272) != 0 && at(half, 289) == 0, "half a cell is filled on the left only");
    CHECK(at(low, 272) != 0 && at(low, 280) == 0, "a low cell is a sliver");
    CHECK(at(empty, 272) == 0, "an empty cell has no fill");
    CHECK(at(half, 272) != at(low, 272), "low is drawn in a warning colour");
    int first = 0;
    CHECK(differing(half, plug, &first) > 10, "charging looks different from running on battery");
    CHECK(differing(none, half, &first) > 100, "and the gauge adds a visible number");
}

// "Quick" as a number. While a song plays, each frame touches only the equalizer and any line that
// is scrolling, a tiny part of the screen. The cover is never resent.
static void test_playing_frames_are_tiny() {
    Deck deck;
    std::vector<uint16_t> px(kScreen * kScreen, 0);
    rk::Surface sf(reinterpret_cast<uint8_t*>(px.data()), kScreen, kScreen, kScreen * 2);
    Scene scene;
    const auto art = fake_art(1);
    deck.set_link(Link::Online);
    deck.set_track(song("A VERY LONG TITLE THAT WILL HAVE TO SCROLL ACROSS THE SCREEN", "ARTIST", "ALBUM", true, 1));
    deck.set_art(art.data(), accent_of(art.data(), kArt * kArt));
    for (int i = 0; i < 90; ++i) {
        deck.update(kDt);
        scene.draw(sf, deck);
    }
    frame_cost(sf);

    long total = 0, biggest = 0;
    int frames = 0, most_boxes = 0;
    for (int i = 0; i < 600; ++i) {
        deck.update(kDt);
        scene.draw(sf, deck);
        int boxes = 0;
        const long c = frame_cost(sf, &boxes);
        total += c, biggest = c > biggest ? c : biggest, most_boxes = boxes > most_boxes ? boxes : most_boxes;
        ++frames;
    }
    CHECK(biggest < 16000, "the largest frame sent %ld pixels, the cover alone is %d", biggest, kArt * kArt);
    CHECK(total / frames < 11000, "an average frame sent %ld pixels", total / frames);
    CHECK(most_boxes <= rk::Surface::kMaxDirty, "within the box limit");

    // Worst case: all three lines scroll at once. Still far below a full repaint (217156 pixels).
    deck.set_track(song("A VERY LONG TITLE THAT WILL HAVE TO SCROLL ACROSS THE SCREEN", "AN EQUALLY LONG ARTIST NAME THAT SCROLLS TOO", "AND AN ALBUM AS LONG AS THE REST OF THEM", true, 2));
    deck.set_art(art.data(), accent_of(art.data(), kArt * kArt));
    deck.update(kDt);
    scene.draw(sf, deck);
    frame_cost(sf);
    long worst = 0;
    for (int i = 0; i < 600; ++i) {
        deck.update(kDt);
        scene.draw(sf, deck);
        worst = std::max(worst, frame_cost(sf));
    }
    CHECK(worst < 30000, "with every line scrolling the largest frame sent %ld pixels", worst);
}

// A short title leaves only the equalizer moving.
static void test_short_title_only_moves_the_bars() {
    Deck deck;
    std::vector<uint16_t> px(kScreen * kScreen, 0);
    rk::Surface sf(reinterpret_cast<uint8_t*>(px.data()), kScreen, kScreen, kScreen * 2);
    Scene scene;
    const auto art = fake_art(1);
    deck.set_link(Link::Online);
    deck.set_track(song("SHORT", "ART", "AL", true, 1));
    deck.set_art(art.data(), accent_of(art.data(), kArt * kArt));
    for (int i = 0; i < 90; ++i) {
        deck.update(kDt);
        scene.draw(sf, deck);
    }
    frame_cost(sf);
    long biggest = 0;
    for (int i = 0; i < 300; ++i) {
        deck.update(kDt);
        scene.draw(sf, deck);
        biggest = std::max(biggest, frame_cost(sf));
    }
    CHECK(biggest > 0 && biggest < 7000, "just the equalizer touched %ld pixels a frame", biggest);
}

// Paused, with short text, the player must go completely quiet: no pixels, no work.
static void test_paused_is_silent() {
    Deck deck;
    std::vector<uint16_t> px(kScreen * kScreen, 0);
    rk::Surface sf(reinterpret_cast<uint8_t*>(px.data()), kScreen, kScreen, kScreen * 2);
    Scene scene;
    const auto art = fake_art(1);
    deck.set_link(Link::Online);
    deck.set_track(song("QUIET", "ARTIST", "ALBUM", false, 1));
    deck.set_art(art.data(), default_accent());
    for (int i = 0; i < 120; ++i) {
        deck.update(kDt);
        scene.draw(sf, deck);
    }
    frame_cost(sf);
    int touched = 0;
    for (int i = 0; i < 300; ++i) {
        deck.update(kDt);
        scene.draw(sf, deck);
        touched += frame_cost(sf) > 0 ? 1 : 0;
    }
    CHECK(touched == 0, "a paused player redrew on %d of 300 frames", touched);
}

static void test_nothing_outside_the_screen() {
    // The Surface clips; a guard band around a bigger buffer proves nothing is written outside.
    const int guard = 64;
    std::vector<uint16_t> big((kScreen + 2 * guard) * (kScreen + 2 * guard), 0xBEEF);
    rk::Surface sf(reinterpret_cast<uint8_t*>(big.data() + guard * (kScreen + 2 * guard) + guard), kScreen, kScreen, (kScreen + 2 * guard) * 2);
    Scene scene;
    Deck deck;
    const auto art = fake_art(3);
    deck.set_link(Link::Online);
    deck.set_track(song("EDGE", "CASE", "ALBUM", true, 1));
    deck.set_art(art.data(), default_accent());
    for (int i = 0; i < 20; ++i) {
        deck.update(kDt);
        scene.draw(sf, deck);
    }
    int stray = 0;
    const int stride = kScreen + 2 * guard;
    for (int y = 0; y < kScreen + 2 * guard; ++y)
        for (int x = 0; x < stride; ++x) {
            const bool inside = x >= guard && x < guard + kScreen && y >= guard && y < guard + kScreen;
            if (!inside && big[size_t(y) * stride + x] != 0xBEEF) ++stray;
        }
    CHECK(stray == 0, "%d pixels written outside the surface", stray);
}

int main() {
    test_incremental_matches_fresh();
    test_cover_is_exact();
    test_cover_has_rounded_corners();
    test_paused_cover_is_dimmer();
    test_glow_and_ring();
    test_each_state_looks_different();
    test_wifi_icon();
    test_battery_gauge();
    test_playing_frames_are_tiny();
    test_short_title_only_moves_the_bars();
    test_paused_is_silent();
    test_nothing_outside_the_screen();
    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all scene checks passed\n");
    return 0;
}
