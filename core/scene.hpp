#pragma once

#include <cstdint>

#include "deck.hpp"
#include "rk/surface.hpp"

namespace cs {

constexpr int kScreen = 466;

// Paints the player into a Surface: the album cover with a soft glow, the song, artist and album
// below it, and a small equalizer while it plays. After the first full paint only the equalizer
// and any line of text that is scrolling are redrawn, and a paused player redraws nothing at all.
class Scene {
public:
    void draw(rk::Surface& sf, const Deck& deck);

private:
    static constexpr int kLines = 3;

    void repaint_all(rk::Surface& sf, const Deck& d);
    void paint_ring(rk::Surface& sf, const Deck& d);
    void paint_glow(rk::Surface& sf, const Deck& d);
    void paint_cover(rk::Surface& sf, const Deck& d);
    void paint_line(rk::Surface& sf, const Deck& d, int line);
    void paint_footer(rk::Surface& sf, const Deck& d);
    void paint_wifi(rk::Surface& sf, const Deck& d);

    uint32_t revision_ = 0;
    int offset_[kLines] = {-1, -1, -1};
    int bars_[kBars] = {-1, -1, -1, -1, -1};
};

}  // namespace cs
