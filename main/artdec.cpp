#include "artdec.hpp"

#include "deck.hpp"
#include "esp_jpeg_dec.h"
#include "esp_log.h"

namespace dev {

static const char* TAG = "art";

bool decode_thumbnail(const uint8_t* jpeg, size_t length, uint16_t* out) {
    constexpr int N = cs::kArt;
    jpeg_dec_config_t cfg = DEFAULT_JPEG_DEC_CONFIG();
    cfg.output_type = JPEG_PIXEL_FORMAT_RGB565_LE;
    jpeg_dec_handle_t dec = nullptr;
    if (jpeg_dec_open(&cfg, &dec) != JPEG_ERR_OK) return false;

    jpeg_dec_io_t io = {};
    io.inbuf = const_cast<uint8_t*>(jpeg);
    io.inbuf_len = int(length);
    jpeg_dec_header_info_t info = {};
    bool ok = false;

    if (jpeg_dec_parse_header(dec, &io, &info) != JPEG_ERR_OK) {
        ESP_LOGW(TAG, "not a jpeg the decoder can read (%u bytes)", unsigned(length));
    } else if (info.width == 0 || info.height == 0 || info.width > 640 || info.height > 640) {
        ESP_LOGW(TAG, "unexpected size %ux%u", info.width, info.height);
    } else {
        int len = 0;
        jpeg_dec_get_outbuf_len(dec, &len);
        uint8_t* pixels = static_cast<uint8_t*>(jpeg_calloc_align(size_t(len), 16));
        if (pixels) {
            io.outbuf = pixels;
            if (jpeg_dec_process(dec, &io) == JPEG_ERR_OK) {
                const uint16_t* src = reinterpret_cast<const uint16_t*>(pixels);
                const int w = info.width, h = info.height;
                // Average each block of source pixels that lands on one thumbnail pixel.
                for (int y = 0; y < N; ++y) {
                    const int y0 = y * h / N, y1 = ((y + 1) * h / N > y0 ? (y + 1) * h / N : y0 + 1);
                    for (int x = 0; x < N; ++x) {
                        const int x0 = x * w / N, x1 = ((x + 1) * w / N > x0 ? (x + 1) * w / N : x0 + 1);
                        int r = 0, g = 0, b = 0, n = 0;
                        for (int yy = y0; yy < y1; ++yy) {
                            for (int xx = x0; xx < x1; ++xx) {
                                const uint16_t p = src[yy * w + xx];
                                r += (p >> 11) & 0x1F, g += (p >> 5) & 0x3F, b += p & 0x1F;
                                ++n;
                            }
                        }
                        out[y * N + x] = uint16_t(((r / n) << 11) | ((g / n) << 5) | (b / n));
                    }
                }
                ok = true;
            }
            jpeg_free_align(pixels);
        }
    }
    jpeg_dec_close(dec);
    return ok;
}

}  // namespace dev
