#pragma once

// CycleCountStepper.h -- the count gesture both Draw surfaces share (the shape windows' curve pen and the timeline
// lane's Draw box): how many whole cycles a periodic shape packs into the width being dragged.
//
// Holding Option freezes the box and turns the pointer's height off where Option went down into steps of kStepPx
// pixels (up = more cycles, down = fewer). The count never leaves 1..most, and it REBASES at both ends: pushing past
// the most (or below 1) drags the base along, so the first step back always changes the count and no step is hidden
// beyond the limit. `step` is the same move for the `[` and `]` keys. Pure arithmetic, no UI. Message thread only.
#include <optional>

namespace synth::ui {

class CycleCountStepper {
public:
    /** Pixels of vertical Option-drag per cycle added or removed. */
    static constexpr float kStepPx = 12.0f;

    struct Count {
        int cycles = 1;    ///< The count to use, always within 1..most.
        bool over = false; ///< More was asked for than fits (the chip says "most that fit").
    };

    /** The count after `delta` more (or fewer) cycles than `now`, held to 1..most. */
    static Count step(int now, int delta, int most) noexcept;

    /** True from the first drag frame with Option down until reset(). */
    bool held() const noexcept { return held_; }
    /** Option went down at height `anchorY` with `current` cycles showing. */
    void begin(float anchorY, int current, int most) noexcept;
    /** The count for the pointer at height `y`, or nullopt while it has not yet moved a whole step. */
    std::optional<Count> dragTo(float y, int most) noexcept;
    /** Option went up (or the drag ended): the next Option press starts from the count then showing. */
    void reset() noexcept { held_ = false; }

private:
    bool held_ = false;
    bool moved_ = false; ///< At least one step was taken since begin(): a return to zero steps still reports.
    float anchorY_ = 0.0f;
    int base_ = 1;
};

} // namespace synth::ui
