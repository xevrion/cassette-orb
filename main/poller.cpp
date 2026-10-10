#include "poller.hpp"

#include <cstring>
#include <initializer_list>

#include "artdec.hpp"
#include "esp_crt_bundle.h"
#include "freertos/idf_additions.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "nowplaying.hpp"
#include "timesync.hpp"

namespace dev {

static const char* TAG = "poll";
#ifdef CASSETTE_URL
static const char* kUrl = CASSETTE_URL;  // a test build can point at a local server
#else
static const char* kUrl = "https://xevrion.dev/api/now-playing";
#endif

namespace {

// One GET into buf. Returns the byte count, or -1 on any failure or a status other than 200.
int http_get(const char* url, uint8_t* buf, size_t cap, int timeout_ms) {
    esp_http_client_config_t cfg = {};
    cfg.url = url;
    cfg.timeout_ms = timeout_ms;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    cfg.buffer_size = 2048;
    cfg.user_agent = "cassette-orb/1.0";
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) return -1;

    int got = -1;
    if (esp_http_client_open(c, 0) == ESP_OK) {
        esp_http_client_fetch_headers(c);
        const int status = esp_http_client_get_status_code(c);
        if (status == 200) {
            got = 0;
            while (size_t(got) < cap) {
                const int r = esp_http_client_read(c, reinterpret_cast<char*>(buf) + got, int(cap) - got);
                if (r < 0) {
                    got = -1;
                    break;
                }
                if (r == 0) break;
                got += r;
            }
        } else {
            ESP_LOGW(TAG, "%s answered %d", url, status);
        }
    }
    esp_http_client_close(c);
    esp_http_client_cleanup(c);
    return got;
}

}  // namespace

Poller::Poller(Wifi& wifi) : wifi_(wifi), lock_(xSemaphoreCreateMutex()) {}

// The stack lives in PSRAM: the TLS handshake wants a big one, and internal RAM is what the display needs.
void Poller::start() { xTaskCreateWithCaps(entry, "poller", 16384, this, 4, nullptr, MALLOC_CAP_SPIRAM); }

void Poller::entry(void* self) { static_cast<Poller*>(self)->run(); }

bool Poller::take(Snapshot& out, uint32_t seq) {
    xSemaphoreTake(lock_, portMAX_DELAY);
    const bool changed = snap_.seq != seq;
    if (changed) out = snap_;
    xSemaphoreGive(lock_);
    return changed;
}

void Poller::publish_link(cs::Link link) {
    xSemaphoreTake(lock_, portMAX_DELAY);
    if (snap_.link != link) {
        snap_.link = link;
        ++snap_.seq;
    }
    xSemaphoreGive(lock_);
}

void Poller::publish_track(const cs::Track& t) {
    xSemaphoreTake(lock_, portMAX_DELAY);
    const bool same = snap_.track_valid && snap_.track.id == t.id && snap_.track.known == t.known && snap_.track.playing == t.playing;
    if (!same) {
        snap_.track = t;
        snap_.track_valid = true;
        ++snap_.seq;
    }
    xSemaphoreGive(lock_);
}

void Poller::publish_art(uint32_t track_id, const uint16_t* art, uint16_t accent) {
    xSemaphoreTake(lock_, portMAX_DELAY);
    snap_.art_for = track_id;
    snap_.art_valid = art != nullptr;
    if (art) {
        std::memcpy(snap_.art.data(), art, snap_.art.size() * sizeof(uint16_t));
        snap_.accent = accent;
    }
    ++snap_.art_seq;
    ++snap_.seq;
    xSemaphoreGive(lock_);
}

void Poller::run() {
    static uint8_t body[4096];
    uint8_t* image = static_cast<uint8_t*>(heap_caps_malloc(96 * 1024, MALLOC_CAP_SPIRAM));
    uint16_t* thumb = static_cast<uint16_t*>(heap_caps_malloc(size_t(cs::kArt) * cs::kArt * sizeof(uint16_t), MALLOC_CAP_SPIRAM));

    bool have_clock = false;
    int fails = 0;
    uint32_t art_done_for = 0;
    uint32_t art_tries_for = 0;
    int art_tries = 0;

    for (;;) {
        if (!wifi_.connected()) {
            publish_link(wifi_.seconds_down() > 25 ? cs::Link::Offline : cs::Link::Connecting);
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }
        if (!have_clock) {
            have_clock = sync_time();
            if (!have_clock) {
                publish_link(cs::Link::Error);
                vTaskDelay(pdMS_TO_TICKS(4000));
                continue;
            }
        }

        bool playing = false;
        const int n = http_get(kUrl, body, sizeof body - 1, 10000);
        cs::Track track;
        if (n > 0 && (body[n] = 0, cs::parse_now_playing(reinterpret_cast<const char*>(body), size_t(n), track))) {
            fails = 0;
            playing = track.known && track.playing;
            publish_link(cs::Link::Online);
            publish_track(track);

            if (track.known && image && thumb && track.id != art_done_for) {
                if (art_tries_for != track.id) art_tries_for = track.id, art_tries = 0;
                if (art_tries++ < 3) {
                    char url[300];
                    bool got = false;
                    for (int size : {300, 64}) {
                        if (!cs::small_art_url(track.art_url, url, sizeof url, size)) continue;
                        const int len = http_get(url, image, 96 * 1024, 12000);
                        if (len > 0 && decode_thumbnail(image, size_t(len), thumb)) {
                            got = true;
                            break;
                        }
                    }
                    if (got) {
                        publish_art(track.id, thumb, cs::accent_of(thumb, cs::kArt * cs::kArt));
                        art_done_for = track.id;
                        ESP_LOGI(TAG, "cover for %s", track.title);
                    } else {
                        ESP_LOGW(TAG, "no cover for %s (try %d)", track.title, art_tries);
                    }
                } else if (art_tries == 4) {
                    publish_art(track.id, nullptr, 0);  // give up quietly and keep the stand-in
                    art_done_for = track.id;
                }
            }
            ESP_LOGI(TAG, "%s: %s - %s", track.known ? (playing ? "playing" : "paused") : "nothing", track.title, track.artist);
        } else {
            ++fails;
            ESP_LOGW(TAG, "poll failed (%d in a row), got %d bytes", fails, n);
            if (fails >= 2) publish_link(cs::Link::Error);
        }

        // Quick while music is on, relaxed otherwise, and backing off while the network is struggling.
        int wait_ms = playing ? 4000 : 8000;
        if (fails) wait_ms = 2000 << (fails < 4 ? fails : 4);
        vTaskDelay(pdMS_TO_TICKS(wait_ms));
    }
}

}  // namespace dev
