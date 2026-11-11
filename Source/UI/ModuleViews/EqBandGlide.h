// EqBandGlide.h -- how a Parametric EQ's drawn bands trail the module's real ones after an undo or redo
// (docs/layout/undo-glide.md "Points, dots and clips"). The module's band settings are final at once; what is DRAWN
// (the dot, the curve through it, the thumbnail's curve) eases from the old settings to the restored ones over
// KeyedTween::kMs. Frequency (in octaves), gain, Q and on/off move together; a band an undo takes away fades out where
// it was and one a redo brings back fades in where it lands. A drag or any other edit lands at once (sync with
// restored = false). Under Reduce Motion nothing travels; the restored band's dot fades in (alphaFor); Animations Off
// and an off-screen owner land plain. Shared by EQCurveComponent (card and EQ window) and MixerEqThumbnail. Message
// thread only.
//
// A band's presence glides in both directions for any change of on/off, not only an undo: an added band grows where it
// was parked and a removed one shrinks away where it was. A change of a band's type or slope runs a separate, longer
// morph (typeMorph) that the owner cross-fades its curve and the dot's colour with.
#pragma once

#include "Modules/FX/ParametricEQModule.h"
#include <array>
#include <cstdint>
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

namespace synth::ui {

// Only named here, so this header stays out of KeyedTween.h's reach (scripts/check-header-reach.sh); the .cpp files
// that call into the tween include UI/Layout/KeyGlide/KeyedTween.h.
template <typename T>
class KeyedTween;
using KeyedPointTween = KeyedTween<juce::Point<float>>;

class EqBandGlide {
public:
    using Bands = ParametricEQModule::BandSnapshots;

    /** What is drawn right now, given the resting (real) settings. */
    struct Drawn {
        /** Per band: enabled = visible now; gain is the dot's own (not yet scaled by presence). */
        Bands dots{};
        /** The bands the response curve is summed from: a fading band's gain is scaled by its presence. */
        Bands curve{};
        std::array<float, ParametricEQModule::kMaxBands> presence{}; // 0..1
        std::array<float, ParametricEQModule::kMaxBands> alpha{};    // dot opacity: presence x the Reduce Motion fade
        float outputGainDb = 0.0f;
        /** 1 right after a band's type or slope changed, easing to 0: the weight of the curve and colours it came from.
         */
        float typeMorph = 0.0f;
    };

    explicit EqBandGlide(juce::Component& owner);
    ~EqBandGlide();

    /** Called for every frame, retarget and clear of the glide: the owner recomputes what it draws and repaints. */
    std::function<void()> onChange;

    /** The settings moved from before to after. With `restored` (an undo or redo) the drawn settings glide from where
     *  they are drawn now; otherwise they land at once (a band switched on or off still fades in or out where it
     *  stands). A changed type or slope starts the type morph either way. */
    void sync(const Bands& before, float outputGainBefore, const Bands& after, float outputGainAfter, bool restored);

    Drawn drawn(const Bands& resting, float outputGain) const;
    bool isAnimating() const noexcept;
    static constexpr double kTypeMorphMs = 220.0;

    KeyedPointTween& tweenForTest() noexcept { return *tween_; }
    KeyedPointTween& morphTweenForTest() noexcept { return *morph_; }
    static std::int64_t placeKey(int band) noexcept { return band * 2; }     // (log2 frequency, gain)
    static std::int64_t shapeKey(int band) noexcept { return band * 2 + 1; } // (Q, presence)
    static constexpr std::int64_t kOutputKey = ParametricEQModule::kMaxBands * 2;

private:
    struct Ghost { // where a band an undo took away is still drawn while it fades out
        bool active = false;
        float freqHz = 1000.0f;
        float gainDb = 0.0f;
        float q = ParametricEQModule::kDefaultQ;
    };

    std::unique_ptr<KeyedPointTween> tween_;
    std::unique_ptr<KeyedPointTween> morph_;
    std::array<bool, ParametricEQModule::kMaxBands> presenceGlide_{}; // a band's fade from a plain edit is running
    std::array<Ghost, ParametricEQModule::kMaxBands> ghost_{};
};

} // namespace synth::ui
