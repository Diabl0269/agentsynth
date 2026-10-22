#pragma once

// Plays one preview sound at a time for the Sounds view. A background thread decodes the file (capped in length,
// converted to the device rate) into memory; the audio thread only reads that finished buffer, so it never allocates,
// locks or touches a file. The engine sums the preview into its output AFTER the master, so the song's volume, mute
// and master effects never reach it, and a bounce never contains it.

#include "Transport/BlockTimeInfo.h"
#include <array>
#include <atomic>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <memory>
#include <mutex>
#include <vector>

namespace synth::soundbrowser {

class SoundPreviewPlayer {
public:
    static constexpr double kMaxSeconds = 60.0; ///< Longest stretch of a file that is decoded.
    static constexpr float kMinGainDb = -60.0f; ///< At or below this the preview is silent.
    static constexpr float kMaxGainDb = 6.0f;
    static constexpr float kDefaultGainDb = -6.0f;

    SoundPreviewPlayer();
    ~SoundPreviewPlayer();

    // ---- Message thread ---------------------------------------------------------------------------------
    /** Cuts the sound that plays (5 ms fade), decodes `file` in the background and plays it as soon as it is ready.
     *  With `alignToBar`, and the song playing, it starts on the next bar. Returns the request's id; a request
     *  that the next one replaces before it finished decoding never plays. A file no reader opens never plays. */
    std::uint64_t play(const juce::File& file, bool alignToBar);
    /** Cuts the sound that plays with a 5 ms fade and drops one still decoding. */
    void stop();
    /** The preview's own level in dB; clamped to -60..+6, and -60 is silence. Any thread. */
    void setGainDb(float db) noexcept;
    float getGainDb() const noexcept { return gainDb_.load(std::memory_order_relaxed); }
    /** While true nothing is mixed and requests are discarded (an offline render must not contain a preview). */
    void setSuspended(bool suspended) noexcept;

    // ---- What the UI shows (any thread) -----------------------------------------------------------------
    bool isPlaying() const noexcept { return playing_.load(std::memory_order_relaxed); }
    /** The id of the request that sounds or waits for its bar; 0 when none. */
    std::uint64_t playingRequestId() const noexcept { return playingId_.load(std::memory_order_relaxed); }
    /** How far through its clip the sounding request is, 0..1. */
    float playheadFraction() const noexcept { return playhead_.load(std::memory_order_relaxed); }

    // ---- Audio thread, and the prepare path --------------------------------------------------------------
    /** Called when the output rate or block size is set; nothing renders while it runs. */
    void prepare(double sampleRate, int maxBlockSize);
    /** Sums the preview into the first `numChannels` channels of `buffer`. `info` is the transport's block info at the
     *  start of the callback. Lock-free and allocation-free. */
    void process(juce::AudioBuffer<float>& buffer, int numChannels, const synth::BlockTimeInfo& info) noexcept;

    // ---- Test seams --------------------------------------------------------------------------------------
    /** Blocks until no decode is running or queued; false on timeout. */
    bool waitForDecodeForTests(int timeoutMs) const;
    /** Clips the message thread still holds (decoded buffers not yet released). */
    int heldClipCountForTests() const;

private:
    struct Clip;
    struct Command;
    struct Voice;
    class Decoder;

    int heldClips() const;
    void post(Command command, const std::shared_ptr<Clip>& owner = {});
    void releaseFinishedClips();
    void drainCommands(const synth::BlockTimeInfo& info) noexcept;
    void startVoice(const Command& command, const synth::BlockTimeInfo& info) noexcept;
    void fadeOutAll() noexcept;
    void resolvePending(int numSamples, const synth::BlockTimeInfo& info) noexcept;
    void mixVoices(juce::AudioBuffer<float>& buffer, int numChannels, int numSamples) noexcept;
    void publishState() noexcept;
    static int samplesUntilNextBar(const synth::BlockTimeInfo& info, int numSamples) noexcept;

    friend class Decoder;

    std::atomic<float> gainDb_{kDefaultGainDb};
    std::atomic<bool> suspended_{false};
    std::atomic<double> sampleRate_{0.0};
    std::atomic<bool> playing_{false};
    std::atomic<std::uint64_t> playingId_{0};
    std::atomic<float> playhead_{0.0f};
    std::atomic<std::uint64_t> liveFloor_{0}; ///< Clips with a lower sequence number are no longer read.

    // Commands: many producers (message thread, decoder thread) under `producerMutex_`, one consumer (audio thread).
    static constexpr int kFifoSize = 16;
    juce::AbstractFifo fifo_{kFifoSize};
    std::unique_ptr<Command[]> commands_;
    mutable std::mutex producerMutex_;
    std::vector<std::shared_ptr<Clip>> clips_; ///< Owned here; guarded by producerMutex_.
    std::uint64_t nextSequence_ = 1;           ///< Guarded by producerMutex_.
    std::atomic<std::uint64_t> requestCounter_{0};

    // Audio-thread state.
    static constexpr int kMaxVoices = 4;
    std::unique_ptr<Voice[]> voices_;
    std::uint64_t lastConsumed_ = 0;
    float gainNow_ = -1.0f;
    int fadeOutSamples_ = 240;
    int fadeInSamples_ = 48;
    double renderRate_ = 0.0;

    std::unique_ptr<Decoder> decoder_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SoundPreviewPlayer)
};

} // namespace synth::soundbrowser
