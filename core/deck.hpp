#pragma once

#include <cstdint>
#include <vector>

#include "nowplaying.hpp"

namespace cs {

enum class Link { Offline, Connecting, Online, Error };

constexpr int kArt = 216;  // the cover is stored at the size it is shown, so it is drawn pixel for pixel
constexpr int kBars = 5;   // bars in the equalizer
constexpr int kBarSteps = 8;

// What the player shows: the current song, its cover, and the little bit of motion that says
// it is playing. No drawing in here, so it can be tested without a screen.
class Deck {
public:
    Deck();

    void set_link(Link link);
    void set_track(const Track& track);
    // The cover for the current song, arriving a moment after its metadata. null removes it.
    void set_art(const uint16_t* rgb565_square, uint16_t accent);
    void update(float dt);
    // percent 0..100, or -1 when no battery is fitted (running from USB, nothing to show).
    void set_battery(int percent, bool charging);

    int battery() const { return battery_; }
    bool charging() const { return charging_; }
    const Track& track() const { return track_; }
    Link link() const { return link_; }
    bool playing() const { return track_.known && track_.playing; }
    bool has_art() const { return has_art_; }
    const uint16_t* art() const { return art_.data(); }
    uint16_t accent() const { return accent_; }

    // Equalizer bar height, 0 to kBarSteps. Bars dance while playing and settle flat when paused.
    int bar(int index) const;
    // Seconds on this song, drives the scrolling text.
    float seconds() const { return seconds_; }
    // Changes whenever something that needs a full repaint changes.
    uint32_t revision() const { return revision_; }

private:
    Track track_;
    Link link_ = Link::Connecting;
    std::vector<uint16_t> art_;
    bool has_art_ = false;
    uint16_t accent_;
    int battery_ = -1;
    bool charging_ = false;
    float energy_ = 0.0f;  // eases between 0 (paused) and 1 (playing)
    float phase_ = 0.0f;
    float seconds_ = 0.0f;
    uint32_t revision_ = 1;
};

// Pixels to scroll a line that is overflow pixels too wide, after seconds on screen.
// Holds still at the start, scrolls to the end, holds, and scrolls back, forever.
int marquee_offset(float seconds, int overflow);

// A saturated colour from a cover, brightened so it glows on black, or the house blue for a dull cover.
uint16_t accent_of(const uint16_t* rgb565, int pixels);

// The colour used when a song has no cover or a dull one.
uint16_t default_accent();

}  // namespace cs
