// CustomFilterEngine.h -- the Filter's Custom type end to end: owns the shape list (message
// thread), hands rendered shape tables to the audio thread lock-free, blends them at the morph
// cursor, redesigns the minimum-phase kernel when the controls move, and convolves.
#pragma once

#include "CustomFilterKernelDesigner.h"
#include "CustomFilterShapes.h"
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
        float depth = 1.0f; // multiplier on every shape's dB
    };

    /** Rendered immutable form of one shape list: kGridSize dB values per shape plus positions. */
    struct ShapeTable {
        int count = 0;
        std::vector<float> db; // count * kGridSize
        std::vector<float> x, y;
    };

    static constexpr int kMaxChannels = PartitionedConvolver::kMaxChannels;
    static constexpr int kGridSize = CustomFilterKernelDesigner::kGridSize;

    CustomFilterEngine();
    ~CustomFilterEngine();
    CustomFilterEngine(const CustomFilterEngine&) = delete;
    CustomFilterEngine& operator=(const CustomFilterEngine&) = delete;

    /** Message thread only. Sanitises, stores and publishes `shapes` to the audio thread. */
    void setShapes(const CustomFilterShapes& shapes);
    /** Message thread only. */
    const CustomFilterShapes& getShapes() const noexcept { return shapes_; }

    /** Allocates every audio buffer. Call from prepareToPlay, never concurrently with process(). */
    void prepare(double sampleRate);
    /** Audio thread. Clears the convolution history (a mode or voice-count change). */
    void resetState() noexcept;
    /** Audio thread. Convolves each non-null `channels[c]` (c < kMaxChannels) in place with the
     *  kernel for `controls`, redesigning it first when needed. No allocation, locks or system calls. */
    void process(float* const* channels, int numChannels, int numSamples, const Controls& controls) noexcept;

    /** Kernels designed since construction (audio thread writes; any thread may read). */
    int getDesignCount() const noexcept { return designCount_.load(std::memory_order_relaxed); }

    /** Blend weights at (x, y) for `table`, written to `out` (table.count values summing to 1). */
    static void blendWeights(const ShapeTable& table, float x, float y, float* out) noexcept;
    /** Renders one shape list into a table. Allocates; message thread. */
    static std::unique_ptr<ShapeTable> renderTable(const CustomFilterShapes& shapes);

private:
    bool needsDesign(const Controls& c) const noexcept;
    void design(const Controls& c, int fadeSamples) noexcept;
    void adoptPendingTable() noexcept;
    void freeRetiredTables() noexcept;

    CustomFilterShapes shapes_; // message thread only

    // Lock-free table handoff: see CustomFilterEngine.cpp.
    std::atomic<ShapeTable*> pending_{nullptr};
    ShapeTable* current_ = nullptr; // audio thread only
    static constexpr int kRetireCapacity = 32;
    std::array<ShapeTable*, kRetireCapacity> retired_{};
    juce::AbstractFifo retireFifo_{kRetireCapacity};

    CustomFilterKernelDesigner designer_;
    PartitionedConvolver convolver_;
    std::vector<float> blendedDb_; // kGridSize
    std::vector<float> weights_;   // CustomFilterShapes::kMaxShapes
    std::vector<float> kernel_;    // kKernelLength
    Controls designed_{};
    bool tableChanged_ = false;
    std::atomic<int> designCount_{0};
};

} // namespace synth
