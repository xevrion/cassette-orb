#include "scene.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "rk/font.hpp"

namespace cs {

namespace {

using rk::rgb565;
using rk::Surface;

constexpr uint16_t kBlack = 0;
constexpr uint16_t kWhite = rgb565(240, 240, 246);
constexpr uint16_t kGrey = rgb565(128, 130, 142);
constexpr uint16_t kDimRing = rgb565(44, 46, 54);

// Layout in a 466 pixel square. The cover is a rounded square, which sits well inside the round panel.
constexpr int kCoverX = 233 - kArt / 2, kCoverY = 52, kCoverRadius = 26;
constexpr int kLineY[3] = {290, 322, 344};
constexpr int kLineScale[3] = {3, 2, 2};
constexpr int kLineX0[3] = {50, 70, 70};
constexpr int kLineX1[3] = {416, 396, 396};
constexpr int kFooterY = 372, kFooterH = 28;
constexpr int kWifiY = 436;  // the dot of the Wi-Fi icon
constexpr int kBarW = 10, kBarGap = 6, kBarStep = 3;

uint16_t scaled(uint16_t c, float k) {
    const float r = float((c >> 11) & 0x1F) * 255.0f / 31.0f, g = float((c >> 5) & 0x3F) * 255.0f / 63.0f, b = float(c & 0x1F) * 255.0f / 31.0f;
    return rgb565(uint8_t(std::min(255.0f, r * k)), uint8_t(std::min(255.0f, g * k)), uint8_t(std::min(255.0f, b * k)));
}

void fill_disc(Surface& sf, int cx, int cy, int r, uint16_t c) {
    for (int dy = -r; dy <= r; ++dy) {
        const int half = int(std::sqrt(float(r * r - dy * dy)) + 0.5f);
        sf.fill_rect(cx - half, cy + dy, 2 * half + 1, 1, c);
    }
}

void thick_line(Surface& sf, int x0, int y0, int x1, int y1, int thickness, uint16_t c) {
    const int steps = std::max(std::abs(x1 - x0), std::abs(y1 - y0));
    const int h = thickness / 2;
    for (int i = 0; i <= steps; ++i) {
        const int x = steps ? x0 + (x1 - x0) * i / steps : x0;
        const int y = steps ? y0 + (y1 - y0) * i / steps : y0;
        sf.fill_rect(x - h, y - h, thickness, thickness, c);
    }
}

// A rounded rectangle, one scanline at a time.
void round_rect(Surface& sf, int x0, int y0, int x1, int y1, int r, uint16_t c) {
    for (int y = y0; y <= y1; ++y) {
        const int edge = std::min(y - y0, y1 - y);
        int inset = 0;
        if (edge < r) {
            const float dy = float(r - edge);
            inset = r - int(std::sqrt(std::max(0.0f, float(r * r) - dy * dy)) + 0.5f);
        }
        sf.fill_rect(x0 + inset, y, x1 - x0 + 1 - 2 * inset, 1, c);
    }
}

// 5-6-5 pixels spread so red/blue and green sit in separate halves of a 32 bit word, which lets
// one multiply dim all three channels at once.
inline uint32_t spread(uint16_t c) {
    const uint32_t v = c;
    return (v | (v << 16)) & 0x07E0F81Fu;
}
inline uint16_t fold(uint32_t v) {
    v &= 0x07E0F81Fu;
    return uint16_t(v | (v >> 16));
}
inline uint16_t dim(uint16_t c, uint32_t t) {  // t is 0..32
    return fold((spread(c) * t) >> 5);
}

}  // namespace

void Scene::paint_ring(Surface& sf, const Deck& d) {
    uint16_t c = kDimRing;
    if (d.link() == Link::Online) c = d.playing() ? d.accent() : kDimRing;
    else if (d.link() == Link::Connecting) c = rgb565(80, 92, 120);
    else c = rgb565(150, 40, 40);
    fill_disc(sf, 233, 233, 228, c);
    fill_disc(sf, 233, 233, 225, kBlack);
}

// A soft halo in the song's colour behind the cover: a few rounded rectangles, each a little
// bigger and fainter than the last.
void Scene::paint_glow(Surface& sf, const Deck& d) {
    const uint16_t base = d.has_art() || d.track().known ? d.accent() : default_accent();
    const float strength = d.playing() ? 1.0f : 0.45f;
    constexpr int kRings = 9;
    for (int i = kRings; i >= 1; --i) {
        const int g = i * 3;
        const float k = strength * 0.30f * (1.0f - float(i) / float(kRings + 1));
        round_rect(sf, kCoverX - g, kCoverY - g, kCoverX + kArt - 1 + g, kCoverY + kArt - 1 + g, kCoverRadius + g, scaled(base, k * 0.5f));
    }
}

// The cover at full size with rounded corners, dimmed while paused. A song with no cover yet
// gets a dark tile in its colour with a note on it.
void Scene::paint_cover(Surface& sf, const Deck& d) {
    const uint32_t t = d.playing() || !d.track().known ? 32 : 17;
    uint16_t row[kArt];
    for (int y = 0; y < kArt; ++y) {
        const int edge = std::min(y, kArt - 1 - y);
        int inset = 0;
        if (edge < kCoverRadius) {
            const float dy = float(kCoverRadius - edge);
            inset = kCoverRadius - int(std::sqrt(std::max(0.0f, float(kCoverRadius * kCoverRadius) - dy * dy)) + 0.5f);
        }
        if (d.has_art()) {
            const uint16_t* src = d.art() + y * kArt;
            for (int x = 0; x < kArt; ++x) row[x] = t == 32 ? src[x] : dim(src[x], t);
        } else {
            const float k = 0.34f - 0.26f * float(y) / float(kArt);
            const uint16_t tile = scaled(d.accent(), k);
            for (int x = 0; x < kArt; ++x) row[x] = tile;
        }
        sf.write_row(kCoverX + inset, kCoverY + y, row + inset, kArt - 2 * inset);
    }

    if (!d.has_art()) {
        // A music note, large and a little lighter than the tile.
        const uint16_t ink = scaled(d.accent(), d.track().known ? 0.9f : 0.55f);
        const int nx = kCoverX + kArt / 2 - 18, ny = kCoverY + kArt / 2 + 38;
        fill_disc(sf, nx, ny, 24, ink);
        sf.fill_rect(nx + 18, ny - 100, 9, 104, ink);
        thick_line(sf, nx + 22, ny - 96, nx + 58, ny - 60, 16, ink);
    }
}

void Scene::paint_line(Surface& sf, const Deck& d, int line) {
    char text[200] = "";
    uint16_t colour = kWhite;
    const Track& t = d.track();
    if (t.known) {
        if (line == 0) std::snprintf(text, sizeof text, "%s", t.title), colour = kWhite;
        else if (line == 1) std::snprintf(text, sizeof text, "%s", t.artist), colour = d.accent();
        else std::snprintf(text, sizeof text, "%s", t.album), colour = kGrey;
    } else {
        if (line == 0) std::snprintf(text, sizeof text, "NOTHING PLAYING");
        else if (line == 1) std::snprintf(text, sizeof text, "PLAY SOMETHING ON SPOTIFY"), colour = kGrey;
    }

    const int scale = kLineScale[line];
    const int box = kLineX1[line] - kLineX0[line] + 1;
    const int width = rk::text_width(text, scale);
    offset_[line] = marquee_offset(d.seconds(), std::max(0, width - box));

    const rk::Rect clip = {kLineX0[line], kLineY[line], kLineX1[line], kLineY[line] + rk::text_height(scale) - 1};
    sf.fill_rect(clip.x0, clip.y0, box, clip.y1 - clip.y0 + 1, kBlack);
    const int x = width <= box ? kLineX0[line] + (box - width) / 2 : kLineX0[line] - offset_[line];
    rk::draw_text(sf, x, kLineY[line], text, scale, colour, &clip);
}

// Bars while playing; a short word when paused or when something is wrong.
void Scene::paint_footer(Surface& sf, const Deck& d) {
    const bool bars_only = d.link() == Link::Online && d.playing();
    // Words need the wide strip; the bars only touch their own patch, so a bar update stays small.
    if (bars_only) sf.fill_rect(233 - 40, kFooterY, 80, kFooterH, kBlack);
    else sf.fill_rect(120, kFooterY, 226, kFooterH, kBlack);

    const char* word = "";
    uint16_t colour = kGrey;
    switch (d.link()) {
    case Link::Offline: word = "NO WI-FI", colour = rgb565(235, 85, 85); break;
    case Link::Connecting: word = "CONNECTING...", colour = rgb565(130, 150, 190); break;
    case Link::Error: word = "NO INTERNET", colour = rgb565(235, 170, 50); break;
    case Link::Online:
        if (d.playing()) break;
        word = d.track().known ? "PAUSED" : "";
        break;
    }
    if (word[0] || !d.playing()) {
        if (word[0]) {
            const int w = rk::text_width(word, 2);
            rk::draw_text(sf, 233 - w / 2, kFooterY + (kFooterH - rk::text_height(2)) / 2, word, 2, colour);
        }
        for (int& b : bars_) b = 0;
        return;
    }

    const int total = kBars * kBarW + (kBars - 1) * kBarGap;
    const int x0 = 233 - total / 2, base = kFooterY + kFooterH - 1;
    for (int i = 0; i < kBars; ++i) {
        const int level = d.bar(i);
        bars_[i] = level;
        const int h = std::max(2, level * kBarStep);
        round_rect(sf, x0 + i * (kBarW + kBarGap), base - h + 1, x0 + i * (kBarW + kBarGap) + kBarW - 1, base, 2, d.accent());
    }
}

// The classic Wi-Fi fan under the footer. Green means joined and reaching the site, amber means
// joined but the site is not answering, one grey-blue arc means still joining, a red dot means
// no network at all. Unlit arcs stay as a faint outline so the shape is always readable.
void Scene::paint_wifi(Surface& sf, const Deck& d) {
    uint16_t lit = kGrey;
    int arcs = 0;
    switch (d.link()) {
    case Link::Online: lit = rgb565(60, 190, 110), arcs = 3; break;
    case Link::Error: lit = rgb565(235, 170, 50), arcs = 3; break;
    case Link::Connecting: lit = rgb565(130, 150, 190), arcs = 1; break;
    case Link::Offline: lit = rgb565(235, 85, 85), arcs = 0; break;
    }
    constexpr int cx = 233, cy = kWifiY;
    constexpr int radii[3] = {10, 18, 26};
    for (int a = 0; a < 3; ++a) {
        const int r = radii[a];
        const uint16_t c = a < arcs ? lit : rgb565(40, 42, 50);
        const int lo = (r - 1) * (r - 1), hi = (r + 2) * (r + 2);
        for (int dy = -r - 2; dy <= -3; ++dy)
            for (int dx = -r - 2; dx <= r + 2; ++dx) {
                const int dist = dx * dx + dy * dy;
                if (dist >= lo && dist < hi && -dy >= std::abs(dx)) sf.fill_rect(cx + dx, cy + dy, 1, 1, c);
            }
    }
    fill_disc(sf, cx, cy, 3, lit);
}

// A small battery with a percentage, to the right of the Wi-Fi icon. Absent when running from
// USB with no battery fitted. Charging shows a bolt over the cell.
void Scene::paint_battery(Surface& sf, const Deck& d) {
    if (d.battery() < 0) return;
    const int pct = d.battery();
    const uint16_t ink = d.charging() ? rgb565(60, 190, 110) : pct <= 15 ? rgb565(235, 85, 85) : pct <= 30 ? rgb565(235, 170, 50) : kWhite;
    constexpr int x = 268, y = 424, w = 26, h = 14;  // the cell; a nub on the right side
    sf.fill_rect(x, y, w, h, kGrey);
    sf.fill_rect(x + 2, y + 2, w - 4, h - 4, kBlack);
    sf.fill_rect(x + w, y + 4, 3, h - 8, kGrey);
    const int inner = w - 6;
    const int fill = pct <= 0 ? 0 : std::max(2, inner * pct / 100);
    sf.fill_rect(x + 3, y + 3, fill, h - 6, ink);
    if (d.charging()) {
        thick_line(sf, x + 14, y + 2, x + 10, y + 7, 2, kWhite);
        thick_line(sf, x + 10, y + 7, x + 16, y + 7, 2, kWhite);
        thick_line(sf, x + 16, y + 7, x + 12, y + 12, 2, kWhite);
    }
    char text[16];
    std::snprintf(text, sizeof text, "%d%%", pct);
    rk::draw_text(sf, x + w + 8, y + (h - rk::text_height(1)) / 2, text, 1, ink);
}

void Scene::repaint_all(Surface& sf, const Deck& d) {
    sf.fill_rect(0, 0, kScreen, kScreen, kBlack);
    paint_ring(sf, d);
    paint_glow(sf, d);
    paint_cover(sf, d);
    for (int i = 0; i < kLines; ++i) paint_line(sf, d, i);
    paint_footer(sf, d);
    paint_wifi(sf, d);
    paint_battery(sf, d);
}

void Scene::draw(Surface& sf, const Deck& d) {
    if (revision_ != d.revision()) {
        revision_ = d.revision();
        repaint_all(sf, d);
        return;
    }

    // A line only needs redrawing when it is long enough to scroll and has moved a whole pixel.
    const Track& t = d.track();
    const char* texts[kLines] = {t.known ? t.title : "NOTHING PLAYING", t.known ? t.artist : "PLAY SOMETHING ON SPOTIFY", t.known ? t.album : ""};
    for (int i = 0; i < kLines; ++i) {
        const int box = kLineX1[i] - kLineX0[i] + 1;
        const int over = std::max(0, rk::text_width(texts[i], kLineScale[i]) - box);
        if (marquee_offset(d.seconds(), over) != offset_[i]) paint_line(sf, d, i);
    }

    if (d.playing() && d.link() == Link::Online) {
        bool changed = false;
        for (int i = 0; i < kBars; ++i) changed = changed || d.bar(i) != bars_[i];
        if (changed) paint_footer(sf, d);
    }
}

}  // namespace cs
