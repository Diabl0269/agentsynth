#pragma once

// CableFlowDots.h
//
// Where the signal-flow dots sit along each cable, kept between paints, and a frame's dots filled one colour at a time
// (docs/layout/rendering.md#an-idle-canvas-with-moving-flow-dots-draws-one-picture).

#include <juce_graphics/juce_graphics.h>

#include <unordered_map>
#include <utility>
#include <vector>

namespace synth::ui {

/** Message thread only. `build` must return the same path for the same two ends every time. */
class CableFlowDots {
public:
    using PathBuilder = juce::Path (*)(juce::Point<float>, juce::Point<float>);
    static constexpr int kDotsPerCable = 3;
    static constexpr float kDotDiameter = 5.0f;

    explicit CableFlowDots(PathBuilder build) noexcept
        : build_(build) {}

    /** Adds the dots of the cable from `p1` to `p2` at `phase` (0 to 1 is one lap) to `out`, leaving out any whose
     *  centre is inside `keepOut`. */
    void addDots(juce::Path& out, juce::Point<float> p1, juce::Point<float> p2, float phase,
                 juce::Rectangle<float> keepOut);
    /** The same dots, kept under `colour` until fill(). */
    void batch(juce::Colour colour, juce::Point<float> p1, juce::Point<float> p2, float phase,
               juce::Rectangle<float> keepOut);
    /** Fills every dot batched since the last fill(), one fill per colour, and forgets them. */
    void fill(juce::Graphics& g);

    /** Test seams: how many cables had their dot track measured, and how many fills the last fill() made. */
    int tracksMeasured() const noexcept { return tracksMeasured_; }
    int lastFillCount() const noexcept { return lastFillCount_; }

private:
    struct Track {
        std::vector<juce::Point<float>> points; // the flattened curve
        std::vector<float> along;               // the distance along it to each point
    };
    struct Key {
        float x1, y1, x2, y2;
        bool operator==(const Key& o) const noexcept;
    };
    struct KeyHash {
        size_t operator()(const Key& k) const noexcept;
    };
    const Track& track(juce::Point<float> p1, juce::Point<float> p2);
    static juce::Point<float> pointAt(const Track& track, float distance) noexcept;

    PathBuilder build_;
    std::unordered_map<Key, Track, KeyHash> tracks_;
    std::vector<std::pair<juce::uint32, juce::Path>> batches_;
    int tracksMeasured_ = 0;
    int lastFillCount_ = 0;
};

} // namespace synth::ui
