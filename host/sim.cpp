// Desktop simulator. Runs the same deck and scene code as the board.
//
//   ./host/build/sim                  window, cycling through made-up songs
//   ./host/build/sim --live           window, showing what your site says is playing right now
//   ./host/build/sim --shots dir      PPM frames of a scripted run
//   ./host/build/sim --frames dir     one song's worth of frames, for a GIF
//
// Keys: N next song, P play or pause, L cycle the link state, Esc quit.

#include <SDL2/SDL.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "deck.hpp"
#include "scene.hpp"

using namespace cs;

namespace {

constexpr float kDt = 1.0f / 30.0f;
const char* kEndpoint = "https://xevrion.dev/api/now-playing";

struct Song {
    const char* title;
    const char* artist;
    const char* album;
};

const Song kSongs[] = {
    {"Blue Eyes", "Yo Yo Honey Singh", "Blue Eyes"},
    {"Midnight City", "M83", "Hurry Up, We're Dreaming"},
    {"A Much Longer Title That Has To Scroll Across The Screen", "An Artist With A Rather Long Name Too", "An Album With A Long Name As Well"},
    {"Halo", "Beyonc\xC3\xA9", "I Am... Sasha Fierce"},
    {"Tum Hi Ho", "Arijit Singh", "Aashiqui 2"},
};

std::vector<uint16_t> fake_art(int seed) {
    std::vector<uint16_t> px(kArt * kArt);
    for (int y = 0; y < kArt; ++y) {
        for (int x = 0; x < kArt; ++x) {
            const float fx = float(x) / kArt, fy = float(y) / kArt;
            const float v = 0.5f + 0.5f * std::sin(fx * 6.0f + float(seed)) * std::cos(fy * 5.0f - float(seed) * 0.7f);
            const uint8_t r = uint8_t(255 * (0.25f + 0.75f * std::fabs(std::sin(float(seed) * 1.7f)) * v));
            const uint8_t g = uint8_t(255 * (0.2f + 0.6f * fy * (1.0f - v * 0.5f)));
            const uint8_t b = uint8_t(255 * (0.3f + 0.7f * std::fabs(std::cos(float(seed) * 0.9f)) * (1.0f - fx * 0.6f)));
            px[y * kArt + x] = rk::rgb565(r, g, b);
        }
    }
    return px;
}

Track make_track(const char* title, const char* artist, const char* album, bool playing, uint32_t id) {
    Track t;
    char raw[400];
    std::snprintf(raw, sizeof raw, "{\"isPlaying\":%s,\"title\":\"%s\",\"artist\":\"%s\",\"album\":\"%s\"}", playing ? "true" : "false", title, artist, album);
    parse_now_playing(raw, std::strlen(raw), t);
    t.id = id;
    return t;
}

// The same thing the board will do, on a laptop: ask the site, then fetch and shrink the cover.
std::string shell(const std::string& cmd, size_t cap = 1 << 20) {
    std::string out;
    if (FILE* p = popen(cmd.c_str(), "r")) {
        char buf[4096];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof buf, p)) > 0 && out.size() < cap) out.append(buf, n);
        pclose(p);
    }
    return out;
}

class LiveFeed {
public:
    LiveFeed() : thread_([this] { loop(); }) {}
    ~LiveFeed() {
        stop_ = true;
        thread_.join();
    }
    bool poll(Track& t, std::vector<uint16_t>& art, bool& has_art, bool& fresh) {
        std::lock_guard<std::mutex> lock(mu_);
        fresh = fresh_;
        fresh_ = false;
        t = track_;
        art = art_;
        has_art = has_art_;
        return ok_;
    }

private:
    void loop() {
        while (!stop_) {
            const std::string body = shell(std::string("curl -s -m 10 ") + kEndpoint, 1 << 16);
            Track t;
            const bool ok = parse_now_playing(body.data(), body.size(), t);
            std::vector<uint16_t> art;
            bool has_art = false;
            if (ok && t.known) {
                char small[300];
                if (small_art_url(t.art_url, small, sizeof small)) {
                    char size[24];
                    std::snprintf(size, sizeof size, "%dx%d!", kArt, kArt);
                    const std::string raw = shell(std::string("curl -s -m 10 '") + small + "' | magick jpeg:- -resize " + size + " -depth 8 rgb:-");
                    if (raw.size() == size_t(kArt * kArt * 3)) {
                        art.resize(kArt * kArt);
                        for (int i = 0; i < kArt * kArt; ++i) art[i] = rk::rgb565(uint8_t(raw[i * 3]), uint8_t(raw[i * 3 + 1]), uint8_t(raw[i * 3 + 2]));
                        has_art = true;
                    }
                }
            }
            {
                std::lock_guard<std::mutex> lock(mu_);
                ok_ = ok;
                track_ = t;
                art_ = art;
                has_art_ = has_art;
                fresh_ = true;
            }
            for (int i = 0; i < 50 && !stop_; ++i) SDL_Delay(100);
        }
    }

    std::mutex mu_;
    std::atomic<bool> stop_{false};
    Track track_;
    std::vector<uint16_t> art_;
    bool has_art_ = false, ok_ = false, fresh_ = false;
    std::thread thread_;
};

void mask_corners(std::vector<uint16_t>& px) {
    const float c = kScreen / 2.0f - 0.5f, r = kScreen / 2.0f;
    for (int y = 0; y < kScreen; ++y)
        for (int x = 0; x < kScreen; ++x) {
            const float dx = float(x) - c, dy = float(y) - c;
            if (dx * dx + dy * dy > r * r) px[y * kScreen + x] = 0;
        }
}

struct Sim {
    Deck deck;
    Scene scene;
    std::vector<uint16_t> fb;
    rk::Surface surface;
    int song = 0;

    Sim() : fb(kScreen * kScreen, 0), surface(reinterpret_cast<uint8_t*>(fb.data()), kScreen, kScreen, kScreen * 2) {}

    void play(int index, bool playing) {
        song = index % int(sizeof kSongs / sizeof kSongs[0]);
        deck.set_track(make_track(kSongs[song].title, kSongs[song].artist, kSongs[song].album, playing, 100 + uint32_t(song)));
        const auto art = fake_art(song + 1);
        deck.set_art(art.data(), accent_of(art.data(), kArt * kArt));
    }

    void tick() {
        deck.update(kDt);
        scene.draw(surface, deck);
        rk::Rect r;
        surface.take_dirty(r);
    }

    void write_ppm(const std::string& path) {
        std::vector<uint16_t> copy = fb;
        mask_corners(copy);
        std::FILE* f = std::fopen(path.c_str(), "wb");
        if (!f) return;
        std::fprintf(f, "P6\n%d %d\n255\n", kScreen, kScreen);
        for (uint16_t p : copy) {
            const unsigned char rgb[3] = {uint8_t((p >> 8) & 0xF8), uint8_t((p >> 3) & 0xFC), uint8_t((p << 3) & 0xF8)};
            std::fwrite(rgb, 1, 3, f);
        }
        std::fclose(f);
    }
};

int run_shots(const std::string& dir) {
    Sim sim;
    int n = 0;
    auto shot = [&](const char* what) {
        char path[256];
        std::snprintf(path, sizeof path, "%s/%02d_%s.ppm", dir.c_str(), n++, what);
        sim.write_ppm(path);
        std::printf("%s\n", path);
    };
    auto run = [&](float seconds) {
        for (int i = 0; i < int(seconds / kDt); ++i) sim.tick();
    };

    sim.deck.set_link(Link::Connecting);
    run(0.3f);
    shot("connecting");
    sim.deck.set_link(Link::Online);
    run(0.3f);
    shot("nothing_playing");
    sim.play(1, true);
    run(2.0f);
    shot("playing");
    run(1.0f);
    shot("playing_later");
    sim.play(2, true);
    run(1.5f);
    shot("long_text_start");
    run(4.0f);
    shot("long_text_scrolled");
    sim.play(3, false);
    run(4.0f);
    shot("paused");
    sim.play(0, true);
    run(2.0f);
    shot("another_song");
    sim.play(4, true);
    run(2.0f);
    shot("and_another");
    sim.deck.set_link(Link::Offline);
    run(0.2f);
    shot("offline");
    return 0;
}

int run_frames(const std::string& dir) {
    Sim sim;
    sim.deck.set_link(Link::Online);
    int n = 0;
    auto grab = [&]() {
        char path[256];
        std::snprintf(path, sizeof path, "%s/f%04d.ppm", dir.c_str(), n++);
        sim.write_ppm(path);
    };
    sim.play(1, true);
    for (int i = 0; i < 90; ++i) sim.tick(), grab();   // spinning up
    sim.play(2, true);
    for (int i = 0; i < 200; ++i) sim.tick(), grab();  // the next song, with text that scrolls
    sim.play(2, false);
    for (int i = 0; i < 60; ++i) sim.tick(), grab();   // pausing: the disc winds down
    std::printf("%d frames\n", n);
    return 0;
}

int run_window(bool live) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "SDL: %s\n", SDL_GetError());
        return 1;
    }
    constexpr int kScale = 2;
    SDL_Window* win = SDL_CreateWindow("cassette", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, kScreen * kScale, kScreen * kScale, 0);
    SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    SDL_Texture* tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING, kScreen, kScreen);
    SDL_SetTextureScaleMode(tex, SDL_ScaleModeNearest);

    Sim sim;
    std::unique_ptr<LiveFeed> feed;
    int link = 2;
    if (live) {
        feed = std::make_unique<LiveFeed>();
        sim.deck.set_link(Link::Connecting);
        std::printf("asking %s\n", kEndpoint);
    } else {
        sim.deck.set_link(Link::Online);
        sim.play(0, true);
        std::printf("N next song   P play/pause   L link state   Esc quit\n");
    }

    float since_song = 0.0f;
    bool playing = true, run = true;
    while (run) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) run = false;
            if (e.type == SDL_KEYDOWN && !live) {
                switch (e.key.keysym.sym) {
                case SDLK_ESCAPE: run = false; break;
                case SDLK_n: sim.play(sim.song + 1, playing), since_song = 0; break;
                case SDLK_p: playing = !playing, sim.play(sim.song, playing); break;
                case SDLK_l:
                    link = (link + 1) % 4;
                    sim.deck.set_link(Link(link));
                    break;
                default: break;
                }
            }
        }
        if (feed) {
            Track t;
            std::vector<uint16_t> art;
            bool has_art = false, fresh = false;
            const bool ok = feed->poll(t, art, has_art, fresh);
            if (fresh) {
                sim.deck.set_link(ok ? Link::Online : Link::Error);
                if (ok) {
                    sim.deck.set_track(t);
                    if (has_art) sim.deck.set_art(art.data(), accent_of(art.data(), kArt * kArt));
                    std::printf("%s: %s - %s%s\n", t.playing ? "playing" : "paused", t.title, t.artist, has_art ? " (with art)" : "");
                }
            }
        } else if (playing && (since_song += kDt) > 14.0f) {
            sim.play(sim.song + 1, true);
            since_song = 0;
        }

        sim.tick();
        void* pixels;
        int pitch;
        SDL_LockTexture(tex, nullptr, &pixels, &pitch);
        std::vector<uint16_t> copy = sim.fb;
        mask_corners(copy);
        for (int y = 0; y < kScreen; ++y) std::memcpy(static_cast<uint8_t*>(pixels) + y * pitch, &copy[y * kScreen], kScreen * 2);
        SDL_UnlockTexture(tex);
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, tex, nullptr, nullptr);
        SDL_RenderPresent(ren);
        SDL_Delay(25);
    }
    SDL_Quit();
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    bool live = false;
    std::string shots, frames;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--live") live = true;
        else if (a == "--shots" && i + 1 < argc) shots = argv[++i];
        else if (a == "--frames" && i + 1 < argc) frames = argv[++i];
    }
    if (!shots.empty()) return run_shots(shots);
    if (!frames.empty()) return run_frames(frames);
    return run_window(live);
}
