#pragma once

#include <atomic>

namespace dev {

// Joins a WPA2-Enterprise network (PEAP with MSCHAPv2 inside) from secrets.h and keeps
// rejoining it, with a pause between tries, whenever the link drops.
class Wifi {
public:
    void begin();
    bool connected() const { return connected_; }
    // Seconds since the link last dropped, or since boot if it never came up.
    int seconds_down() const;

private:
    static void on_event(void* self, const char* base, int id, void* data);

    std::atomic<bool> connected_{false};
    std::atomic<long long> down_since_us_{0};
};

}  // namespace dev
