#pragma once

#include <cstdint>
#include <vector>

#include "deck.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "wifi.hpp"

namespace dev {

struct Snapshot {
    uint32_t seq = 0;  // changes whenever anything below changes
    cs::Link link = cs::Link::Connecting;
    bool track_valid = false;
    cs::Track track;
    uint32_t art_seq = 0;  // changes when a new cover arrives
    uint32_t art_for = 0;  // the track id the cover belongs to
    bool art_valid = false;
    uint16_t accent = 0;
    std::vector<uint16_t> art = std::vector<uint16_t>(size_t(cs::kArt) * cs::kArt, 0);  // heap, so it lands in PSRAM
};

// Asks the site what is playing, in its own task so a slow network can never stall the animation.
class Poller {
public:
    explicit Poller(Wifi& wifi);
    void start();
    // Copies the latest news into out if anything changed since seq; returns whether it did.
    bool take(Snapshot& out, uint32_t seq);

private:
    static void entry(void* self);
    void run();
    void publish_link(cs::Link link);
    void publish_track(const cs::Track& t);
    void publish_art(uint32_t track_id, const uint16_t* art, uint16_t accent);

    Wifi& wifi_;
    SemaphoreHandle_t lock_;
    Snapshot snap_;
};

}  // namespace dev
