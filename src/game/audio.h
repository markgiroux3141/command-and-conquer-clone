#pragma once
#include <atomic>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace game {

// SDL2-backed sound mixer. Decodes Westwood AUDs (via fmt::AudFile), resamples
// each to the output device rate, and mixes any number of concurrent one-shot
// sound effects plus one music track. Public calls are made from the main
// thread; the actual mixing runs on SDL's audio callback thread (guarded with
// the device lock). If the device can't be opened the mixer stays silent and
// every call is a harmless no-op, so callers never need to special-case audio.
class AudioMixer {
public:
    ~AudioMixer();
    // Opens the audio device (needs SDL_INIT_AUDIO). Returns false if audio is
    // unavailable; the mixer then stays silent.
    bool init();
    void shutdown();

    void setSoundDir(std::string dir) { soundDir_ = std::move(dir); }
    void setMusicDir(std::string dir) { musicDir_ = std::move(dir); }
    void setEvaDir(std::string dir) { evaDir_ = std::move(dir); }

    // Play a one-shot effect (AUD base name, no extension) at volume 0..255.
    void playSound(const std::string& name, int volume = 255);

    // Speech: routed through a single channel so lines never overlap (a new
    // line replaces the current one, like the original's speak queue).
    // EVA computer line (`<name>.aud` from the eva dir).
    void playEva(const std::string& name);
    // Unit acknowledgement: `<name>.v0N` from the sound dir, cycling through
    // `variations` (the .v00-.v03 response takes) for variety.
    void playVoice(const std::string& name, int variations);

    // Start a music track (AUD base name from the scores dir), replacing the
    // current one. A single track does not loop; poll musicPlaying() and start
    // the next track to drive a jukebox.
    void playMusic(const std::string& name);
    void stopMusic();

    bool enabled() const { return dev_ != 0; }
    bool musicPlaying() const { return musicActive_.load(); }

private:
    // A decoded sound, resampled to the device rate, 16-bit signed mono.
    struct Sound { std::vector<int16_t> pcm; };
    struct Voice { const std::vector<int16_t>* pcm = nullptr; size_t pos = 0; int volume = 255; };

    // Loads + caches an AUD (full filename incl. extension) from `dir`;
    // nullptr on failure. Main thread only.
    const Sound* load(const std::string& dir, const std::string& file);
    void playSpeech(const std::string& dir, const std::string& file);
    void mix(int16_t* out, int frames);
    static void callback(void* userdata, uint8_t* stream, int len);

    uint32_t dev_ = 0;   // SDL_AudioDeviceID (0 = not open)
    int rate_ = 22050;
    std::unordered_map<std::string, Sound> cache_;
    std::vector<Voice> voices_; // overlapping one-shot SFX
    Voice music_;
    Voice speech_;              // single EVA/unit-voice channel (newest wins)
    int voiceCycle_ = 0;        // rotates .v0N acknowledgement variations
    std::atomic<bool> musicActive_{false};
    std::string soundDir_, musicDir_, evaDir_;
};

} // namespace game
