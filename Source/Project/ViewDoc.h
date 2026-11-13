#pragma once

#include <juce_core/juce_core.h>

namespace synth {

/** The graph canvas view a project persists: zoom and pan. Presentation only -- it is saved so a project
 *  reopens looking the way it was left, and is never an edit (no undo step, no unsaved-changes mark). */
struct ViewDoc {
    /** The wheel-zoom range; GraphEditor clamps its own zoom to the same limits. */
    static constexpr float kMinZoom = 0.1f;
    static constexpr float kMaxZoom = 2.0f;
    /** A pan beyond this is a corrupt file, not a view; it clamps. Canvas units are pixels. */
    static constexpr float kMaxPan = 100000.0f;

    float zoom = 1.0f;
    float panX = 0.0f;
    float panY = 0.0f;

    juce::var toVar() const;

    /** All-or-nothing: returns false and leaves `*this` untouched unless `v` is an object whose three numbers
     *  are present and finite. Finite but out-of-range values clamp (zoom to the wheel range, pan to
     *  +-kMaxPan) rather than refuse the file. */
    bool fromVar(const juce::var& v);

    bool operator==(const ViewDoc& other) const noexcept;
    bool operator!=(const ViewDoc& other) const noexcept { return !(*this == other); }
};

} // namespace synth
