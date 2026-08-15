#pragma once

#include "BlockTimeInfo.h"
#include "TempoMap.h"
#include <array>
#include <atomic>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>

namespace synth {

struct TimelineSnapshot;

// The one clock (TL1). Owns play state, sample position, BPM, time signature and
// loop bounds (in beats). The message thread posts commands through a lock-free
// SPSC FIFO; the audio thread drains them at the top of every callback via tick(),
// which also publishes the block's BlockTimeInfo. No locks, no allocation on the
// audio path.
//
// TransportService is a juce::AudioPlayHead: AudioEngine installs it once with
// graph.setPlayHead(), and juce::AudioProcessorGraph re-applies it to every node
// processor on every render pass — so every module (however it was created:
// fresh drop, preset load, undo restore) can read the transport through the
// standard getPlayHead() API with no per-node injection bookkeeping.
//
// Threading contract:
//   - play()/stop()/locateBeat()/setLoop()/setBpm()/setTimeSignature(): one
//     producer thread only (the message thread). Non-blocking; returns false if
//     the FIFO is full and the command was dropped.
//   - prepare()/tick()/getCurrentBlockInfo(): audio thread (or any single thread
//     with no concurrent tick, e.g. an offline render driver).
//   - getPositionSnapshot()/getPosition(): any thread (seqlock read).
class TransportService : public juce::AudioPlayHead {
public:
    TransportService();

    // -- Message-thread API (single producer) -------------------------------
    bool play();
    bool stop();
    bool locateBeat(double beat);
    bool setLoop(double startBeat, double endBeat, bool enabled);
    bool setBpm(double newBpm);
    bool setTimeSignature(int numerator, int denominator);

    // -- Audio-thread API ----------------------------------------------------
    // Called from the engine's prepareToPlay path (never concurrently with
    // tick). Preserves the musical position: the sample position is re-derived
    // from the current beat position at the new sample rate.
    void prepare(double sampleRate, int maxBlockSize);

    // Advance the transport by one block. Drains pending commands (in posting
    // order), publishes the block's BlockTimeInfo and the cross-thread position
    // snapshot, then advances the position (handling loop wrap). Commands take
    // effect at sample 0 of the block; sample-accurate event scheduling within a
    // block is the consumers' job, driven by the returned BlockTimeInfo.
    const BlockTimeInfo& tick(int numSamples);

    // The BlockTimeInfo published by the most recent tick(). Audio thread only.
    const BlockTimeInfo& getCurrentBlockInfo() const noexcept { return currentBlock; }

    // -- This block's timeline snapshot (TL3-1) ------------------------------
    // The playhead is the ONLY thing every module already has a pointer to, so it is also how a
    // module reaches the timeline: AudioEngine::renderNextBlock opens the block's snapshot
    // (TimelineSnapshotExchange::beginAudioBlock(), exactly once per callback — the epoch
    // reclamation contract) and parks the borrowed pointer here, right after tick(). A module then
    // downcasts getPlayHead() to TransportService and reads getCurrentBlockInfo() and
    // getCurrentTimelineSnapshot() together, from the same object.
    //
    // Threading: written and read on the AUDIO THREAD ONLY, in that order, within one callback —
    // hence a plain pointer and not an atomic. No other thread may touch it.
    //
    // Lifetime: the pointee is owned by the exchange and is valid for the CURRENT BLOCK ONLY.
    // Caching it in a module member and reading it next block is a use-after-free (the exchange
    // frees a superseded snapshot two callbacks after it retires it). It is also legitimately
    // null — nothing has been published yet, a build with the timeline compiled out, or a module
    // driven by a bare TransportService in a unit test — so every reader must null-check.
    void setCurrentTimelineSnapshot(const TimelineSnapshot* snapshot) noexcept { currentTimelineSnapshot = snapshot; }
    const TimelineSnapshot* getCurrentTimelineSnapshot() const noexcept { return currentTimelineSnapshot; }

    // -- Any-thread reads ----------------------------------------------------
    struct PositionSnapshot {
        double ppq = 0.0;
        std::int64_t samplePosition = 0;
        double bpm = 120.0;
        double sampleRate = 44100.0;
        int timeSigNumerator = 4;
        int timeSigDenominator = 4;
        bool playing = false;
        bool looping = false;
        double loopStartPpq = 0.0;
        double loopEndPpq = 0.0;
    };

    // Consistent snapshot of the position at the start of the current block.
    PositionSnapshot getPositionSnapshot() const noexcept;

    // juce::AudioPlayHead — modules call getPlayHead()->getPosition() in
    // processBlock; implemented off the snapshot so it is safe from any thread.
    juce::Optional<juce::AudioPlayHead::PositionInfo> getPosition() const override;

    // Audio thread only (races with prepare() elsewhere); other threads read the
    // sample rate from getPositionSnapshot().
    double getSampleRate() const noexcept { return tempoMap.getSampleRate(); }

    static constexpr double kMinBpm = 5.0;
    static constexpr double kMaxBpm = 990.0;
    static constexpr double kMinLoopLengthBeats = 1.0 / 16.0;

private:
    struct Command {
        enum class Type : int { Play, Stop, LocateBeat, SetLoop, SetBpm, SetTimeSignature };
        Type type = Type::Stop;
        double a = 0.0;
        double b = 0.0;
        int i = 0;
        int j = 0;
        bool flag = false;
    };

    bool postCommand(const Command& command) noexcept;
    void drainCommands() noexcept;
    void applyCommand(const Command& command) noexcept;
    void publishSnapshot() noexcept;

    // -- Audio-thread state ---------------------------------------------------
    ConstantTempoMap tempoMap;
    double ppqPosition = 0.0;        // canonical position, in beats
    std::int64_t samplePosition = 0; // always consistent with ppqPosition under tempoMap
    bool playing = false;
    bool looping = false;
    double loopStartPpq = 0.0;
    double loopEndPpq = 4.0;
    int timeSigNumerator = 4;
    int timeSigDenominator = 4;
    BlockTimeInfo currentBlock;

    // Borrowed, never owned; valid for the current block only. See setCurrentTimelineSnapshot.
    const TimelineSnapshot* currentTimelineSnapshot = nullptr;

    // -- Command FIFO (message thread -> audio thread) ------------------------
    static constexpr int kFifoCapacity = 256;
    juce::AbstractFifo fifo{kFifoCapacity};
    std::array<Command, kFifoCapacity> commandSlots;

    // -- Seqlock-published snapshot (audio thread -> any thread) --------------
    // Individual atomics (no torn fields) + a version counter for cross-field
    // consistency. seq_cst everywhere: this runs once per block, so the cost is
    // noise and the correctness argument stays one line long.
    struct SharedPosition {
        std::atomic<double> ppq{0.0};
        std::atomic<std::int64_t> samplePosition{0};
        std::atomic<double> bpm{120.0};
        std::atomic<double> sampleRate{44100.0};
        std::atomic<int> timeSigNumerator{4};
        std::atomic<int> timeSigDenominator{4};
        std::atomic<bool> playing{false};
        std::atomic<bool> looping{false};
        std::atomic<double> loopStartPpq{0.0};
        std::atomic<double> loopEndPpq{4.0};
    };
    SharedPosition shared;
    std::atomic<std::uint32_t> sharedVersion{0}; // odd while a write is in flight

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TransportService)
};

} // namespace synth
