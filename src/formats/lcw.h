// Westwood LCW ("format80") decompression, shared by SHP frames and the
// [MapPack]/[OverlayPack] chunks in RA map INIs.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace fmt {

// Decompresses LCW data starting at src[srcOffset] into dest. dest must be
// pre-sized to the expected output length; decoding stops at the terminator
// or when dest is full (the final copy command may nominally overrun).
// Throws std::runtime_error on malformed input.
void lcwDecode(const std::vector<uint8_t>& src, size_t srcOffset, std::vector<uint8_t>& dest);

} // namespace fmt
