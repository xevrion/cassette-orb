#pragma once

#include <cstddef>
#include <cstdint>

namespace cs {

struct Track {
    bool known = false;    // there is a song to show, even if nothing is playing right now
    bool playing = false;
    char title[96] = "";   // already folded to what the pixel font can draw
    char artist[96] = "";
    char album[96] = "";   // only present while a song is actually playing
    char art_url[200] = "";
    uint32_t id = 0;       // changes when the song changes
};

// Pulls the fields we use out of the site's now-playing JSON. Returns false when the text is
// not a JSON object at all (a truncated download, an HTML error page); out is left alone then.
// An object without a title is fine and gives known == false.
bool parse_now_playing(const char* json, size_t len, Track& out);

// A smaller version of a Spotify cover URL (sizes 64 or 300), or false when the URL is not one we recognise.
bool small_art_url(const char* url, char* out, size_t capacity, int size = 300);

}  // namespace cs
