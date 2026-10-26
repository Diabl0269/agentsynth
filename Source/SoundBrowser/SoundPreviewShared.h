#pragma once

// The types SoundPreviewPlayer.cpp and SoundPreviewDecoder.cpp share; private to the player.

#include "SoundBrowser/SoundPreviewPlayer.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <optional>

namespace synth::soundbrowser {

struct SoundPreviewPlayer::Clip {
    juce::AudioBuffer<float> data; ///< One or two channels at the output rate.
    double sampleRate = 0.0;
    std::uint64_t sequence = 0;
};

struct SoundPreviewPlayer::Command {
    enum class Kind : std::uint8_t { Stop, Play };
    Kind kind = Kind::Stop;
    const Clip* clip = nullptr;
    std::uint64_t requestId = 0;
    bool alignToBar = false;
    std::uint64_t sequence = 0;
};

struct SoundPreviewPlayer::Voice {
    enum class State : std::uint8_t { Free, Pending, Sounding, Fading };
    const Clip* clip = nullptr;
    std::uint64_t sequence = 0;
    std::uint64_t requestId = 0;
    State state = State::Free;
    int pos = 0;
    int startOffset = 0;
    int fadeLeft = 0;
    int fadeInDone = 0;
};

/** Decodes the newest requested file on its own thread and posts the finished clip to the player. */
class SoundPreviewPlayer::Decoder : private juce::Thread {
public:
    explicit Decoder(SoundPreviewPlayer& owner);
    ~Decoder() override;

    void request(const juce::File& file, bool alignToBar, std::uint64_t requestId, const LoopTarget& target);
    void cancel();
    bool waitIdle(int timeoutMs) const;

private:
    struct Request {
        juce::File file;
        bool alignToBar = false;
        std::uint64_t id = 0;
        LoopTarget target;
    };

    void run() override;
    bool superseded(std::uint64_t generation) const { return generation_.load() != generation; }
    std::shared_ptr<Clip> decode(const Request& request, std::uint64_t generation);
    static void fitToTarget(Clip& clip, const LoopTarget& target);

    SoundPreviewPlayer& owner_;
    juce::AudioFormatManager formats_;
    mutable std::mutex mutex_;
    std::optional<Request> pending_;
    std::atomic<std::uint64_t> generation_{0};
    std::atomic<bool> busy_{false};
    juce::WaitableEvent wake_;
};

} // namespace synth::soundbrowser
