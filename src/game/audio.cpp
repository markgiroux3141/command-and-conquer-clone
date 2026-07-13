// SDL2 sound mixer: AUD -> device-rate mono PCM, mixed on the audio thread.

#include "game/audio.h"

#include <SDL.h>

#include <algorithm>

#include "formats/aud.h"

namespace game {

namespace {
// Decode an AudFile's PCM to mono 16-bit at its own sample rate.
std::vector<int16_t> toMono16(const fmt::AudFile& aud) {
    std::vector<int16_t> out;
    int ch = std::max(1, aud.channels);
    if (aud.sampleBits == 16) {
        size_t frames = aud.pcm.size() / (2 * ch);
        out.resize(frames);
        const auto* p = reinterpret_cast<const int16_t*>(aud.pcm.data());
        for (size_t i = 0; i < frames; i++) {
            int acc = 0;
            for (int c = 0; c < ch; c++)
                acc += p[i * ch + c];
            out[i] = int16_t(acc / ch);
        }
    } else { // 8-bit unsigned
        size_t frames = aud.pcm.size() / ch;
        out.resize(frames);
        for (size_t i = 0; i < frames; i++) {
            int acc = 0;
            for (int c = 0; c < ch; c++)
                acc += int(aud.pcm[i * ch + c]) - 128;
            out[i] = int16_t((acc / ch) << 8);
        }
    }
    return out;
}

// Linear-resample mono 16-bit from srcRate to dstRate.
std::vector<int16_t> resample(const std::vector<int16_t>& src, int srcRate, int dstRate) {
    if (srcRate == dstRate || src.empty())
        return src;
    double ratio = double(srcRate) / dstRate;
    size_t outN = size_t(src.size() / ratio);
    std::vector<int16_t> out(outN);
    for (size_t i = 0; i < outN; i++) {
        double sp = i * ratio;
        size_t idx = size_t(sp);
        double frac = sp - idx;
        int a = src[idx];
        int b = idx + 1 < src.size() ? src[idx + 1] : a;
        out[i] = int16_t(a + (b - a) * frac);
    }
    return out;
}
} // namespace

AudioMixer::~AudioMixer() { shutdown(); }

bool AudioMixer::init() {
    if (dev_)
        return true;
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0)
        return false;
    SDL_AudioSpec want{}, have{};
    want.freq = rate_;
    want.format = AUDIO_S16SYS;
    want.channels = 1;
    want.samples = 1024;
    want.callback = &AudioMixer::callback;
    want.userdata = this;
    dev_ = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (!dev_)
        return false;
    rate_ = have.freq; // device may have picked a different rate
    SDL_PauseAudioDevice(dev_, 0);
    return true;
}

void AudioMixer::shutdown() {
    if (!dev_)
        return;
    SDL_CloseAudioDevice(dev_);
    dev_ = 0;
}

const AudioMixer::Sound* AudioMixer::load(const std::string& dir, const std::string& name) {
    std::string key = dir + "/" + name;
    auto it = cache_.find(key);
    if (it != cache_.end())
        return it->second.pcm.empty() ? nullptr : &it->second;
    Sound s;
    try {
        fmt::AudFile aud = fmt::AudFile::load(key + ".aud");
        s.pcm = resample(toMono16(aud), aud.sampleRate, rate_);
    } catch (...) {
        // Cache the failure (empty pcm) so we don't retry the disk every shot.
    }
    Sound& stored = cache_.emplace(key, std::move(s)).first->second;
    return stored.pcm.empty() ? nullptr : &stored;
}

void AudioMixer::playSound(const std::string& name, int volume) {
    if (!dev_ || name.empty())
        return;
    const Sound* s = load(soundDir_, name);
    if (!s)
        return;
    SDL_LockAudioDevice(dev_);
    voices_.push_back({&s->pcm, 0, std::clamp(volume, 0, 255)});
    SDL_UnlockAudioDevice(dev_);
}

void AudioMixer::playMusic(const std::string& name) {
    if (!dev_ || name.empty())
        return;
    const Sound* s = load(musicDir_, name);
    SDL_LockAudioDevice(dev_);
    music_ = {s ? &s->pcm : nullptr, 0, 255};
    musicActive_.store(s != nullptr);
    SDL_UnlockAudioDevice(dev_);
}

void AudioMixer::stopMusic() {
    if (!dev_)
        return;
    SDL_LockAudioDevice(dev_);
    music_ = {};
    musicActive_.store(false);
    SDL_UnlockAudioDevice(dev_);
}

void AudioMixer::mix(int16_t* out, int frames) {
    for (int i = 0; i < frames; i++)
        out[i] = 0;
    // Music at reduced volume so effects sit on top.
    if (music_.pcm) {
        for (int i = 0; i < frames && music_.pos < music_.pcm->size(); i++, music_.pos++)
            out[i] = int16_t(std::clamp(out[i] + (*music_.pcm)[music_.pos] * 3 / 8, -32768, 32767));
        if (music_.pos >= music_.pcm->size()) {
            music_ = {};
            musicActive_.store(false);
        }
    }
    for (auto& v : voices_) {
        for (int i = 0; i < frames && v.pos < v.pcm->size(); i++, v.pos++) {
            int s = (*v.pcm)[v.pos] * v.volume / 255;
            out[i] = int16_t(std::clamp(out[i] + s, -32768, 32767));
        }
    }
    voices_.erase(std::remove_if(voices_.begin(), voices_.end(),
                                 [](const Voice& v) { return v.pos >= v.pcm->size(); }),
                  voices_.end());
}

void AudioMixer::callback(void* userdata, uint8_t* stream, int len) {
    static_cast<AudioMixer*>(userdata)->mix(reinterpret_cast<int16_t*>(stream),
                                            len / int(sizeof(int16_t)));
}

} // namespace game
