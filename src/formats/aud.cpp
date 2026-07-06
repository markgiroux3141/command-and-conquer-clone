// AUD decoder, ported from OpenRA's AudReader.cs, ImaAdpcmReader.cs and
// WestwoodCompressedReader.cs (GPL v3).
//
// Header: u16 sampleRate, i32 dataSize, i32 outputSize, u8 flags (1=stereo,
// 2=16bit), u8 format (1=Westwood ADPCM, 99=IMA ADPCM). Then chunks:
// u16 compressedSize, u16 outputSize, u32 magic 0xDEAF, data.

#include "formats/aud.h"

#include <algorithm>
#include <fstream>
#include <stdexcept>

namespace fmt {
namespace {

const int kImaIndexAdjust[8] = {-1, -1, -1, -1, 2, 4, 6, 8};
const int kImaStepTable[89] = {
    7,     8,     9,     10,    11,    12,    13,    14,    16,    17,    19,
    21,    23,    25,    28,    31,    34,    37,    41,    45,    50,    55,
    60,    66,    73,    80,    88,    97,    107,   118,   130,   143,   157,
    173,   190,   209,   230,   253,   279,   307,   337,   371,   408,   449,
    494,   544,   598,   658,   724,   796,   876,   963,   1060,  1166,  1282,
    1411,  1552,  1707,  1878,  2066,  2272,  2499,  2749,  3024,  3327,  3660,
    4026,  4428,  4871,  5358,  5894,  6484,  7132,  7845,  8630,  9493,  10442,
    11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
    32767};

int16_t decodeImaSample(uint8_t b, int& index, int& current) {
    bool negative = (b & 8) != 0;
    b &= 7;
    int delta = kImaStepTable[index] * b / 4 + kImaStepTable[index] / 8;
    if (negative)
        delta = -delta;
    current = std::clamp(current + delta, -32768, 32767);
    index = std::clamp(index + kImaIndexAdjust[b], 0, 88);
    return int16_t(current);
}

const int kWsStep2[4] = {-2, -1, 0, 1};
const int kWsStep4[16] = {-9, -8, -6, -5, -4, -3, -2, -1, 0, 1, 2, 3, 4, 5, 6, 8};

// One chunk of Westwood ADPCM -> 8-bit unsigned PCM. Chunks are independent
// (the running sample resets to 0x80 each chunk).
void decodeWsChunk(const uint8_t* in, size_t inLen, uint8_t* out, size_t outLen) {
    if (inLen == outLen) { // uncompressed chunk
        std::copy(in, in + inLen, out);
        return;
    }
    int sample = 0x80;
    size_t r = 0, w = 0;
    auto put = [&](int v) {
        if (w < outLen)
            out[w++] = uint8_t(v);
    };
    while (r < inLen && w < outLen) {
        int count = in[r] & 0x3f;
        int op = in[r++] >> 6;
        switch (op) {
        case 0: // 2-bit deltas, 4 samples per byte
            for (count++; count > 0 && r < inLen; count--) {
                int code = in[r++];
                for (int shift = 0; shift < 8; shift += 2) {
                    sample = std::clamp(sample + kWsStep2[(code >> shift) & 3], 0, 255);
                    put(sample);
                }
            }
            break;
        case 1: // 4-bit deltas, 2 samples per byte
            for (count++; count > 0 && r < inLen; count--) {
                int code = in[r++];
                sample = std::clamp(sample + kWsStep4[code & 0x0f], 0, 255);
                put(sample);
                sample = std::clamp(sample + kWsStep4[code >> 4], 0, 255);
                put(sample);
            }
            break;
        case 2:
            if (count & 0x20) { // 5-bit signed delta, one sample
                sample += int(int8_t(count << 3)) >> 3;
                put(sample);
            } else { // raw copy
                for (count++; count > 0 && r < inLen; count--) {
                    sample = in[r++];
                    put(sample);
                }
            }
            break;
        default: // repeat current sample
            for (count++; count > 0; count--)
                put(sample);
            break;
        }
    }
}

} // namespace

AudFile AudFile::load(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f)
        throw std::runtime_error("cannot open AUD: " + path);
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)),
                              std::istreambuf_iterator<char>());
    if (data.size() < 12)
        throw std::runtime_error("AUD too short: " + path);

    auto u16 = [&](size_t off) { return uint16_t(data.at(off) | (data.at(off + 1) << 8)); };
    auto u32 = [&](size_t off) {
        return uint32_t(u16(off)) | (uint32_t(u16(off + 2)) << 16);
    };

    AudFile aud;
    aud.sampleRate = u16(0);
    int32_t dataSize = int32_t(u32(2));
    uint32_t outputSize = u32(6);
    uint8_t flags = data[10];
    uint8_t format = data[11];
    aud.channels = (flags & 1) ? 2 : 1;
    aud.sampleBits = format == 99 ? 16 : 8; // decoded width
    if (format != 1 && format != 99)
        throw std::runtime_error("AUD: unknown format " + std::to_string(format) + ": " + path);

    aud.pcm.reserve(outputSize);
    size_t pos = 12;
    int imaIndex = 0, imaCurrent = 0;

    while (dataSize > 0 && pos + 8 <= data.size()) {
        uint16_t compSize = u16(pos);
        uint16_t chunkOut = u16(pos + 2);
        if (u32(pos + 4) != 0xdeaf)
            throw std::runtime_error("AUD: bad chunk magic: " + path);
        pos += 8;
        if (pos + compSize > data.size())
            throw std::runtime_error("AUD: chunk overruns file: " + path);

        if (format == 99) {
            // IMA state persists across chunks; two nibbles per byte, but the
            // final byte may only use its low nibble.
            for (uint16_t n = 0; n < compSize; n++) {
                uint8_t b = data[pos + n];
                int16_t t = decodeImaSample(b, imaIndex, imaCurrent);
                aud.pcm.push_back(uint8_t(t));
                aud.pcm.push_back(uint8_t(t >> 8));
                if (aud.pcm.size() < outputSize) {
                    t = decodeImaSample(b >> 4, imaIndex, imaCurrent);
                    aud.pcm.push_back(uint8_t(t));
                    aud.pcm.push_back(uint8_t(t >> 8));
                }
            }
        } else {
            size_t start = aud.pcm.size();
            aud.pcm.resize(start + chunkOut);
            decodeWsChunk(data.data() + pos, compSize, aud.pcm.data() + start, chunkOut);
        }

        pos += compSize;
        dataSize -= 8 + compSize;
    }
    return aud;
}

} // namespace fmt
