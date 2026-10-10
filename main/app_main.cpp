#include <cstdio>
#include <vector>

#include "bsp/esp32_s3_touch_amoled_1_75c.h"
#include "deck.hpp"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "poller.hpp"
#include "rk/battery.hpp"
#include "rk/display_lock.hpp"
#include "rk/gesture.hpp"
#include "rk/panel.hpp"
#include "scene.hpp"
#include "wifi.hpp"

static const char* TAG = "cassette";

constexpr int kFrameMs = 16;
constexpr int kBright = 70;       // percent while music is on
constexpr int kDim = 10;          // percent after the music has been off for a while
constexpr int kDimAfterS = 90;
constexpr int kTouchWakeS = 30;   // a tap keeps the screen bright this long
constexpr int kBatteryEvery = 600;  // frames between battery reads, about ten seconds
constexpr int kBlinkFrames = 10;  // full brightness for this many frames when a refresh is asked for

// How many times LVGL actually refreshed the panel, and how long each took, so "smooth" is a number.
static int64_t g_refresh_start = 0, g_refresh_total = 0;
static int g_refreshes = 0;

static void on_refresh(lv_event_t* e) {
    const int64_t now = esp_timer_get_time();
    if (lv_event_get_code(e) == LV_EVENT_REFR_START) {
        g_refresh_start = now;
    } else {
        g_refresh_total += now - g_refresh_start;
        ++g_refreshes;
    }
}

extern "C" void app_main() {
    if (!bsp_display_start()) {
        ESP_LOGE(TAG, "display failed to start");
        return;
    }
    bsp_display_brightness_set(kBright);

    rk::Panel panel;
    {
        rk::DisplayLock lock;
        lv_obj_set_style_bg_color(lv_screen_active(), lv_color_black(), 0);
        lv_obj_remove_flag(lv_screen_active(), LV_OBJ_FLAG_SCROLLABLE);
        if (!panel.begin(lv_screen_active())) {
            ESP_LOGE(TAG, "out of memory for the canvas");
            return;
        }
    }

    // Wi-Fi keeps its calibration data in NVS.
    esp_err_t nvs = nvs_flash_init();
    if (nvs == ESP_ERR_NVS_NO_FREE_PAGES || nvs == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    lv_display_add_event_cb(lv_display_get_default(), on_refresh, LV_EVENT_REFR_START, nullptr);
    lv_display_add_event_cb(lv_display_get_default(), on_refresh, LV_EVENT_REFR_READY, nullptr);

    static cs::Deck deck;
    static cs::Scene scene;
    static dev::Wifi wifi;
    static dev::Poller poller(wifi);
    static dev::Snapshot snap;
    wifi.begin();
    poller.start();

    uint32_t seq = 0, art_seq = 0;
    int64_t last = esp_timer_get_time();
    int quiet_frames = 0;
    int level = kBright;
    int frame = 0;
    lv_indev_t* touch = bsp_display_get_input_dev();
    rk::Battery battery;
    const bool have_battery = battery.begin();
    rk::Gesture gesture;
    int awake_frames = 0, blink = 0;
    int64_t draw_sum = 0, draw_max = 0;
    int draw_n = 0, presented = 0;

#ifdef CASSETTE_DEMO
    // idf.py build -DCASSETTE_DEMO=1: pretend a song with a long title is playing, to time the busiest frames.
    {
        cs::Track t;
        t.known = t.playing = true;
        t.id = 7;
        std::snprintf(t.title, sizeof t.title, "A VERY LONG TITLE THAT WILL HAVE TO SCROLL ACROSS THE SCREEN");
        std::snprintf(t.artist, sizeof t.artist, "AN EQUALLY LONG ARTIST NAME");
        static std::vector<uint16_t> art(size_t(cs::kArt) * cs::kArt);
        for (int i = 0; i < cs::kArt * cs::kArt; ++i) art[i] = rk::rgb565(uint8_t((i % cs::kArt) * 255 / cs::kArt), uint8_t((i / cs::kArt) * 255 / cs::kArt), 160);
        deck.set_link(cs::Link::Online);
        deck.set_track(t);
        deck.set_art(art.data(), cs::accent_of(art.data(), cs::kArt * cs::kArt));
    }
#endif

#ifdef CASSETTE_TRACE
    // idf.py build -DCASSETTE_TRACE=1 logs the largest free internal DMA block four times a second.
    xTaskCreate(
        [](void*) {
            for (;;) {
                ESP_LOGW("trace", "largest dma block %u, internal free %u", unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_DMA)),
                         unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)));
                vTaskDelay(pdMS_TO_TICKS(250));
            }
        },
        "trace", 3072, nullptr, 2, nullptr);
#endif

    TickType_t wake = xTaskGetTickCount();
    for (;;) {
        if (++frame % (kFrameMs ? 5000 / kFrameMs : 150) == 0) {
            ESP_LOGI(TAG, "heap: internal %u free (low %u), dma %u, psram %u | draw avg %.1f ms worst %.1f | we painted %d of %d frames, panel %.1f refreshes/s at %.1f ms",
                     unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)), unsigned(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL)),
                     unsigned(heap_caps_get_free_size(MALLOC_CAP_DMA)), unsigned(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),
                     draw_n ? double(draw_sum) / draw_n / 1000.0 : 0.0, double(draw_max) / 1000.0, presented, draw_n, double(g_refreshes) / 5.0,
                     g_refreshes ? double(g_refresh_total) / g_refreshes / 1000.0 : 0.0);
            draw_sum = draw_max = 0, draw_n = presented = 0;
            g_refreshes = 0, g_refresh_total = 0;
        }
#ifndef CASSETTE_DEMO
        if (poller.take(snap, seq)) {
            seq = snap.seq;
            deck.set_link(snap.link);
            if (snap.track_valid) deck.set_track(snap.track);
            if (snap.art_seq != art_seq) {
                art_seq = snap.art_seq;
                // Only keep a cover that belongs to the song on the label.
                if (snap.art_for == deck.track().id) deck.set_art(snap.art_valid ? snap.art.data() : nullptr, snap.accent);
            }
        }
#endif

        if (have_battery && frame % kBatteryEvery == 1) {
            int percent = 0;
            bool charging = false;
            switch (battery.read(percent, charging)) {
            case rk::Battery::Reading::Level: deck.set_battery(percent, charging); break;
            case rk::Battery::Reading::NoBattery: deck.set_battery(-1, false); break;
            case rk::Battery::Reading::Error: break;  // keep what is on screen
            }
        }

        const int64_t now = esp_timer_get_time();
        float dt = float(now - last) / 1e6f;
        last = now;
        if (dt > 0.1f) dt = 0.1f;
        deck.update(dt);

        {
            const int64_t t0 = esp_timer_get_time();
            rk::DisplayLock lock;
            scene.draw(panel.surface(), deck);
            if (panel.present()) ++presented;
            const int64_t spent = esp_timer_get_time() - t0;
            draw_sum += spent, ++draw_n;
            draw_max = spent > draw_max ? spent : draw_max;
        }

        // A tap wakes a dimmed screen. A long press asks the site again right now, and the screen
        // flashes up once so you know it heard you.
        bool pressed = false;
        lv_point_t point = {0, 0};
        if (touch) {
            rk::DisplayLock lock;
            pressed = lv_indev_get_state(touch) == LV_INDEV_STATE_PRESSED;
            lv_indev_get_point(touch, &point);
        }
        const rk::Tap tap = gesture.update(pressed, point.x, point.y, dt);
        if (pressed || tap == rk::Tap::Short) awake_frames = kTouchWakeS * 1000 / kFrameMs;
        if (tap == rk::Tap::Long) {
            poller.poke();
            blink = kBlinkFrames;
            awake_frames = kTouchWakeS * 1000 / kFrameMs;
        }
        if (awake_frames > 0) --awake_frames;
        if (blink > 0) --blink;

        // Dim the screen when nothing has been playing for a while, and wake it with the music.
        quiet_frames = deck.playing() ? 0 : quiet_frames + 1;
        int want = quiet_frames * kFrameMs / 1000 > kDimAfterS && awake_frames == 0 ? kDim : kBright;
        if (blink > 0) want = 100;
        if (want != level) {
            level = want;
            bsp_display_brightness_set(level);
        }
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(kFrameMs));
    }
}
