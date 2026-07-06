#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace fmt {

// Westwood .AUD audio, decoded to PCM.
// Format 99 (IMA ADPCM) decodes to 16-bit signed; format 1 (Westwood ADPCM)
// decodes to 8-bit unsigned.
struct AudFile {
    int sampleRate = 0;
    int channels = 1;
    int sampleBits = 16;
    std::vector<uint8_t> pcm; // interleaved little-endian

    static AudFile load(const std::string& path);
};

} // namespace fmt
