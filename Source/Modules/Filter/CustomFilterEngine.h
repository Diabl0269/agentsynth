// CustomFilterEngine.h -- the Filter's Custom type end to end: owns the shape list and the per-note
// settings (message thread), hands both to the audio thread lock-free, blends the shapes at the
// morph cursor (one shared cursor, or one per poly voice), redesigns the minimum-phase kernels when
// the controls move, and convolves.
#pragma once

#include "CustomFilterKernelDesigner.h"
#include "CustomFilterPerNote.h"
#include "CustomFilterShapes.h"
#include "CustomFilterVoiceCursors.h"
#include "Modules/Shapes/LockFreeHandoff.h"
#include "PartitionedConvolver.h"
#include <array>
#include <atomic>
#include <memory>
#include <vector>

namespace synth {

class CustomFilterEngine {
public:
    /** Control-rate inputs, one value per block. */
    struct Controls {
        float morphX = 0.5f; // [0, 1]
        float morphY = 0.5f; // [0, 1]
        float shiftSemitones = 0.0f;
        float depth = 1.0f;      // multiplier on every shape's dB
        float cutoffHz = 440.0f; // moves the response by 12 * log2(cutoffHz / 440) semitones, on top of shift
        float resonance = 0.0f;  // [0, 1]: a bell at cutoffHz, added after depth
    };

    /** Rendered immutable form of one shape list: kGridSize dB values per shape plus positions. */
    struct ShapeTable {
        int count = 0;
        std::vector<float> db; // count * kGridSize
        std::vector<float> x, y;
        std::vector<int> orderByX; // shape indices, left to right on the map (ties by index)
    };

    using VoiceInput = CustomFilterVoiceCursors::VoiceInput;

    static constexpr int kMaxChannels = PartitionedConvolver::kMaxChannels;
    /** Channel c of process() belongs to poly voice c % kMaxVoices (both legs of a voice share its cursor). */
    static constexpr int kMaxVoices = CustomFilterVoiceCursors::kMaxVoices;
    static constexpr int kGridSize = CustomFilterKernelDesigner::kGridSize;

    CustomFilterEngine();
    ~CustomFilterEngine();
    CustomFilterEngine(const CustomFilterEngine&) = delete;
    CustomFilterEngine& operator=(const CustomFilterEngine&) = delete;

    /** Message thread only. Sanitises, stores and publishes `shapes` to the audio thread. */
    void setShapes(const CustomFilterShapes& shapes);
    /** Message thread only. */
    const CustomFilterShapes& getShapes() const noexcept { return shapes_; }
    /** Bumped by every setShapes(); a view polls it to learn the list changed under it (undo, a preset, the
     *  editor window). */
    int getShapesGeneration() const noexcept { return shapesGeneration_.load(std::memory_order_relaxed); }

    /** Message thread only. Stores and publishes the per-note settings; bumps getShapesGeneration(). */
    void setPerNote(const CustomFilterPerNote& perNote);
    /** Message thread only. */
    const CustomFilterPerNote& getPerNote() const noexcept { return perNote_; }

    /** Allocates every audio buffer. Call from prepareToPlay, never concurrently with process(). */
    void prepare(double sampleRate);
    /** Audio thread. Clears the convolution history (a mode or voice-count change). */
    void resetState() noexcept;
    /** Audio thread. Convolves each non-null `channels[c]` (c < kMaxChannels) in place with the
     *  kernel for `controls`, redesigning it first when needed. No allocation, locks or system calls.
     *  `voices` (one per poly voice, `numVoices` <= kMaxVoices) turns on the per-note mode when it is not
     *  Off: each voice then gets its own cursor and kernel; null (mono) keeps the one shared kernel. */
    void process(float* const* channels, int numChannels, int numSamples, const Controls& controls,
                 const VoiceInput* voices = nullptr, int numVoices = 0) noexcept;
    /** Most voice kernels designed in one block of `numSamples` (the shared kernel counts against it). */
    static int voiceDesignBudget(int numSamples) noexcept;
    /** Audio thread / tests: voice `v`'s cursor (the shared one while it has had no note). */
    const CustomFilterVoiceCursors& getVoiceCursors() const noexcept { return cursors_; }

    /** Kernels designed since construction (audio thread writes; any thread may read). */
    int getDesignCount() const noexcept { return designCount_.load(std::memory_order_relaxed); }

    /** Blend weights at (x, y) for `table`, written to `out` (table.count values summing to 1). */
    static void blendWeights(const ShapeTable& table, float x, float y, float* out) noexcept;
    /** Renders one shape list into a table. Allocates; message thread. */
    static std::unique_ptr<ShapeTable> renderTable(const CustomFilterShapes& shapes);

private:
    // A control has moved when it changed by more than these since its kernel was designed.
    static constexpr float kMorphEpsilon = 1.0e-4f;
    static constexpr float kShiftEpsilon = 1.0e-3f; // semitones
    static constexpr float kDepthEpsilon = 1.0e-4f;
    static constexpr float kResonanceEpsilon = 1.0e-4f;

    /** True when shift, depth, cutoff or resonance differ by more than their epsilons (morph is separate). */
    static bool toneMoved(const Controls& a, const Controls& b) noexcept;
    bool needsDesign(const Controls& c) const noexcept;
    void design(const Controls& c, int fadeSamples) noexcept;
    void designKernel(const Controls& c) noexcept;
    CustomFilterVoiceCursors::Settings readPerNoteSettings() noexcept;
    void processPerVoice(float* const* channels, int numChannels, int numSamples, const Controls& controls,
                         const VoiceInput* voices, int numVoices, const CustomFilterVoiceCursors::Settings& s) noexcept;
    void engageVoices(int numVoices, int numChannels) noexcept;
    void designVoices(const Controls& controls, int numVoices, int numChannels, int numSamples, int budget) noexcept;
    bool voiceIsIdle(int v, int numChannels) const noexcept;
    bool groupZeroIsHeard(int numChannels) const noexcept;
    void leavePerVoice() noexcept;

    CustomFilterShapes shapes_; // message thread only

    LockFreeHandoff<ShapeTable> tables_; // message thread publishes, audio thread adopts

    CustomFilterKernelDesigner designer_;
    PartitionedConvolver convolver_;
    std::vector<float> blendedDb_; // kGridSize
    std::vector<float> weights_;   // CustomFilterShapes::kMaxShapes
    std::vector<float> kernel_;    // kKernelLength
    Controls designed_{};
    bool tableChanged_ = false;
    std::atomic<int> designCount_{0};
    std::atomic<int> shapesGeneration_{0};

    // Per-note settings: see CustomFilterEngine.cpp (setPerNote / readPerNoteSettings).
    CustomFilterPerNote perNote_; // message thread only
    std::atomic<int> perNoteMode_{0};
    std::atomic<float> perNoteSmoothness_{CustomFilterPerNote::kDefaultSmoothness};
    juce::SpinLock keysLock_;
    std::array<int8_t, CustomFilterPerNote::kNumKeys> pendingKeys_ = CustomFilterPerNote::makeUnassignedKeys();
    bool keysPending_ = false; // under keysLock_
    std::array<int8_t, CustomFilterPerNote::kNumKeys> audioKeys_ =
        CustomFilterPerNote::makeUnassignedKeys(); // audio thread

    // Per-voice kernels (audio thread): voice v convolves through convolver group v + 1 once it has had a note.
    CustomFilterVoiceCursors cursors_;
    std::array<bool, kMaxVoices> voiceOnOwnGroup_{};
    std::array<bool, kMaxVoices> voiceTableDirty_{};
    std::array<Controls, kMaxVoices> voiceDesigned_{};
    bool perVoiceActive_ = false;
};

} // namespace synth
