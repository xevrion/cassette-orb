#include "deck.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "rk/surface.hpp"

namespace cs {

namespace {

constexpr uint16_t kHouseBlue = rk::rgb565(64, 190, 255);
constexpr float kHold = 1.6f;
constexpr float kScrollSpeed = 42.0f;  // pixels per second

}  // namespace

uint16_t default_accent() { return kHouseBlue; }

Deck::Deck() : art_(size_t(kArt) * kArt, 0), accent_(kHouseBlue) {}

void Deck::set_link(Link link) {
    if (link == link_) return;
    link_ = link;
    ++revision_;
}

void Deck::set_track(const Track& t) {
    const bool changed_song = t.id != track_.id || t.known != track_.known;
    const bool changed_state = t.playing != track_.playing;
    track_ = t;
    if (changed_song) {
        has_art_ = false;
        accent_ = kHouseBlue;
        seconds_ = 0.0f;
    }
    if (changed_song || changed_state) ++revision_;
}

void Deck::set_art(const uint16_t* px, uint16_t accent) {
    if (px) {
        std::memcpy(art_.data(), px, art_.size() * sizeof(uint16_t));
        has_art_ = true;
        accent_ = accent;
    } else {
        has_art_ = false;
        accent_ = kHouseBlue;
    }
    ++revision_;
}

void Deck::update(float dt) {
    const float target = playing() ? 1.0f : 0.0f;
    energy_ += (target - energy_) * std::min(1.0f, dt * 5.0f);
    if (target == 0.0f && energy_ < 0.01f) energy_ = 0.0f;
    if (energy_ > 0.0f) phase_ = std::fmod(phase_ + dt, 1000.0f);
    seconds_ += dt;
}

int Deck::bar(int index) const {
    if (energy_ <= 0.0f) return 0;
    // Each bar runs on its own slow wave, so together they look like music without being random.
    static const float kSpeed[kBars] = {5.3f, 7.1f, 4.2f, 6.4f, 8.0f};
    static const float kOffset[kBars] = {0.0f, 1.9f, 3.7f, 5.1f, 2.6f};
    const int i = std::clamp(index, 0, kBars - 1);
    const float wave = 0.5f + 0.5f * std::sin(phase_ * kSpeed[i] + kOffset[i]);
    const float wave2 = 0.5f + 0.5f * std::sin(phase_ * kSpeed[i] * 1.9f + kOffset[i] * 0.7f);
    const float v = energy_ * (0.15f + 0.85f * (0.65f * wave + 0.35f * wave2));
    return std::clamp(int(v * float(kBarSteps) + 0.5f), 1, kBarSteps);
}

int marquee_offset(float seconds, int overflow) {
    if (overflow <= 0) return 0;
    const float travel = float(overflow) / kScrollSpeed;
    const float cycle = 2.0f * (kHold + travel);
    float t = std::fmod(seconds, cycle);
    if (t < kHold) return 0;
    t -= kHold;
    if (t < travel) return int(t * kScrollSpeed);
    t -= travel;
    if (t < kHold) return overflow;
    t -= kHold;
    return overflow - int(t * kScrollSpeed);
}

uint16_t accent_of(const uint16_t* px, int n) {
    // Score each pixel by how colourful and bright it is and keep the best few, so one stray
    // pixel cannot decide the tint.
    float best[8][3] = {};
    float score[8] = {};
    int count = 0;
    for (int i = 0; i < n; ++i) {
        const float r = float((px[i] >> 11) & 0x1F) * 255.0f / 31.0f;
        const float g = float((px[i] >> 5) & 0x3F) * 255.0f / 63.0f;
        const float b = float(px[i] & 0x1F) * 255.0f / 31.0f;
        const float hi = std::max({r, g, b}), lo = std::min({r, g, b});
        const float s = (hi > 0 ? (hi - lo) / hi : 0.0f) * hi;
        int slot = -1;
        if (count < 8) slot = count++;
        else {
            int worst = 0;
            for (int k = 1; k < 8; ++k)
                if (score[k] < score[worst]) worst = k;
            if (s > score[worst]) slot = worst;
        }
        if (slot >= 0) score[slot] = s, best[slot][0] = r, best[slot][1] = g, best[slot][2] = b;
    }
    if (count == 0) return kHouseBlue;
    float r = 0, g = 0, b = 0;
    for (int k = 0; k < count; ++k) r += best[k][0], g += best[k][1], b += best[k][2];
    r /= float(count), g /= float(count), b /= float(count);
    const float hi = std::max({r, g, b}), lo = std::min({r, g, b});
    // A grey or muddy cover would give a dull ring; use the house colour instead.
    if (hi < 90.0f || (hi - lo) < 40.0f) return kHouseBlue;
    const float k = 235.0f / hi;
    return rk::rgb565(uint8_t(std::min(255.0f, r * k)), uint8_t(std::min(255.0f, g * k)), uint8_t(std::min(255.0f, b * k)));
}

}  // namespace cs
