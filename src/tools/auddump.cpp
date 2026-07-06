// auddump — decode a Westwood .AUD to a .WAV file.
//   auddump <file.aud> <out.wav>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <stdexcept>

#include "formats/aud.h"

namespace {

void writeWav(const std::string& path, const fmt::AudFile& aud) {
    std::ofstream f(path, std::ios::binary);
    if (!f)
        throw std::runtime_error("cannot write: " + path);

    auto w16 = [&](uint16_t v) { f.write(reinterpret_cast<char*>(&v), 2); };
    auto w32 = [&](uint32_t v) { f.write(reinterpret_cast<char*>(&v), 4); };

    uint32_t dataLen = uint32_t(aud.pcm.size());
    uint16_t blockAlign = uint16_t(aud.channels * aud.sampleBits / 8);

    f.write("RIFF", 4);
    w32(36 + dataLen);
    f.write("WAVEfmt ", 8);
    w32(16);
    w16(1); // PCM
    w16(uint16_t(aud.channels));
    w32(uint32_t(aud.sampleRate));
    w32(uint32_t(aud.sampleRate) * blockAlign);
    w16(blockAlign);
    w16(uint16_t(aud.sampleBits));
    f.write("data", 4);
    w32(dataLen);
    f.write(reinterpret_cast<const char*>(aud.pcm.data()), dataLen);
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: auddump <file.aud> <out.wav>\n");
        return 2;
    }
    try {
        auto aud = fmt::AudFile::load(argv[1]);
        double seconds = double(aud.pcm.size()) /
                         (aud.channels * (aud.sampleBits / 8) * aud.sampleRate);

        // report RMS so silence/garbage is obvious without listening
        double sumSq = 0;
        size_t n = 0;
        if (aud.sampleBits == 16) {
            for (size_t i = 0; i + 1 < aud.pcm.size(); i += 2, n++) {
                int16_t s = int16_t(aud.pcm[i] | (aud.pcm[i + 1] << 8));
                sumSq += double(s) * s;
            }
            n = n ? n : 1;
        } else {
            for (uint8_t b : aud.pcm)
                sumSq += (double(b) - 128) * (double(b) - 128) * 256 * 256, n++;
            n = n ? n : 1;
        }
        double rms = std::sqrt(sumSq / double(n));

        writeWav(argv[2], aud);
        std::printf("%s: %d Hz, %d-bit, %d ch, %.2fs, rms=%.0f -> %s\n", argv[1],
                    aud.sampleRate, aud.sampleBits, aud.channels, seconds, rms, argv[2]);
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}
