#pragma once

#include <cstddef>
#include <cstdint>

namespace dev {

// Decodes a JPEG and shrinks it to the player's cover size (cs::kArt square) as RGB565.
// Any source up to 640 pixels square works; a 300 pixel cover halves exactly. out must hold
// cs::kArt * cs::kArt pixels. Returns false when the data is not a JPEG the decoder can read.
bool decode_thumbnail(const uint8_t* jpeg, size_t length, uint16_t* out);

}  // namespace dev
