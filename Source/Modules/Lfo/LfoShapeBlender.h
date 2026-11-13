// LfoShapeBlender.h -- the LFO's Custom wave end to end: owns the shape list (message thread), hands rendered wave
// tables to the audio thread lock-free, and blends them at the morph cursor into the one table the LFO plays.
#pragma once

#include "LfoCustomShapes.h"
#include "Modules/Shapes/LockFreeHandoff.h"
#include <array>
#include <atomic>
#include <memory>
#include <vector>

namespace synth {

class LfoShapeBlender {
public:
    /** Rendered immutable form of one shape list: a wave table per shape plus map positions. */
    struct ShapeTables {
        int count = 0;
        std::vector<LfoCustomWave::Table> tables;
        std::vector<float> x, y;
    };

    LfoShapeBlender();

    /** Message thread only. Sanitises, stores and publishes `shapes` to the audio thread. */
    void setShapes(const LfoCustomShapes& shapes);
    /** Message thread only. */
    const LfoCustomShapes& getShapes() const noexcept { return shapes_; }
    /** Bumped by every setShapes(); a view polls it to learn the list changed under it (undo, a preset, the
     *  editor window). */
    int getGeneration() const noexcept { return generation_.load(std::memory_order_relaxed); }

    /** Audio thread. Adopts a newly published list, then re-blends the playing table when the list or the cursor
     *  (each coordinate in [0, 1]) changed since the last blend. With one shape the table is that shape's own, and
     *  the cursor is ignored. No allocation, locks or system calls. */
    const LfoCustomWave::Table& blend(float morphX, float morphY) noexcept;

    /** The shapes blended at (x, y) as a table. Allocates; message thread (the editor's "new shape here"). */
    static LfoCustomWave::Table blendedTable(const LfoCustomShapes& shapes, float x, float y);
    /** Renders one shape list. Allocates; message thread. */
    static std::unique_ptr<ShapeTables> renderTables(const LfoCustomShapes& shapes);

private:
    static void blendInto(const ShapeTables& tables, float x, float y, float* weights,
                          LfoCustomWave::Table& out) noexcept;

    LfoCustomShapes shapes_; // message thread only
    std::atomic<int> generation_{0};
    LockFreeHandoff<ShapeTables> tables_;

    // Audio thread only.
    LfoCustomWave::Table playing_{};
    std::array<float, morph::kMaxShapes> weights_{};
    float blendedX_ = 0.0f;
    float blendedY_ = 0.0f;
    bool dirty_ = true;
};

} // namespace synth
