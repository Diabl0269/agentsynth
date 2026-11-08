// EqBandGlide.h -- how a Parametric EQ's drawn bands trail the module's real ones after an undo or redo
// (docs/layout/undo-glide.md "Points, dots and clips"). The module's band settings are final at once; what is DRAWN
// (the dot, the curve through it, the thumbnail's curve) eases from the old settings to the restored ones over
// KeyedTween::kMs. Frequency (in octaves), gain, Q and on/off move together; a band an undo takes away fades out where
// it was and one a redo brings back fades in where it lands. A drag or any other edit lands at once (sync with
// restored = false). Under Reduce Motion nothing travels; the restored band's dot fades in (alphaFor); Animations Off
// and an off-screen owner land plain. Shared by EQCurveComponent (card and EQ window) and MixerEqThumbnail. Message
// thread only.
#pragma once

#include "Modules/FX/ParametricEQModule.h"
#include "UI/Layout/KeyGlide/KeyedTween.h"
#include <array>
#include <functional>

namespace synth::ui {

class EqBandGlide {
public:
    using Bands = std::array<ParametricEQModule::BandSnapshot, ParametricEQModule::kNumBands>;

    /** What is drawn right now, given the resting (real) settings. */
    struct Drawn {
        /** Per band: enabled = visible now; gain is the dot's own (not yet scaled by presence). */
        Bands dots{};
        /** The bands the response curve is summed from: a fading band's gain is scaled by its presence. */
        Bands curve{};
        std::array<float, ParametricEQModule::kNumBands> presence{}; // 0..1
        std::array<float, ParametricEQModule::kNumBands> alpha{};    // dot opacity: presence x the Reduce Motion fade
        float outputGainDb = 0.0f;
    };

    explicit EqBandGlide(juce::Component& owner);

    /** Called for every frame, retarget and clear of the glide: the owner recomputes what it draws and repaints. */
    std::function<void()> onChange;

    /** The settings moved from before to after. With `restored` (an undo or redo) the drawn settings glide from where
     *  they are drawn now; otherwise they land at once and any running glide ends. */
    void sync(const Bands& before, float outputGainBefore, const Bands& after, float outputGainAfter, bool restored);

    Drawn drawn(const Bands& resting, float outputGain) const;
    bool isAnimating() const noexcept { return tween_.isAnimating(); }

    KeyedPointTween& tweenForTest() noexcept { return tween_; }
    static KeyedPointTween::Key placeKey(int band) noexcept { return band * 2; }     // (log2 frequency, gain)
    static KeyedPointTween::Key shapeKey(int band) noexcept { return band * 2 + 1; } // (Q, presence)
    static constexpr KeyedPointTween::Key kOutputKey = ParametricEQModule::kNumBands * 2;

private:
    struct Ghost { // where a band an undo took away is still drawn while it fades out
        bool active = false;
        float freqHz = 1000.0f;
        float gainDb = 0.0f;
        float q = ParametricEQModule::kDefaultQ;
    };

    KeyedPointTween tween_;
    std::array<Ghost, ParametricEQModule::kNumBands> ghost_{};
};

} // namespace synth::ui
