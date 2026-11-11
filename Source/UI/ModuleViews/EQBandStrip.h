#pragma once

#include "EQCurveComponent.h"
#include "TestMode/TestModeStateSource.h"
#include "UI/Layout/ArrowKeyNavigation.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <map>
#include <memory>
#include <vector>

namespace synth::ui {

template <typename T>
class KeyedTween;

/** The chips under an EQ curve: one per band that is on (plus the selected band when it is off, dimmed) in frequency
 *  order, and a trailing + chip that adds a band. A chip is tinted by the band's kind; pressing it selects the band on
 *  the curve, and the selection follows the curve both ways. Chips glide to their new places when bands pass each other
 *  in frequency, and wrap to further rows when the width asks for it. Slot identity stays the band index: the order is
 *  for display only. Units: EQBandStrip.cpp. */
class EQBandStrip
    : public juce::Component
    , public synth::testmode::StateSource
    , private EQCurveComponent::Listener {
public:
    explicit EQBandStrip(EQCurveComponent& curve);
    ~EQBandStrip() override;

    /** The height the chips need at `width` (one row, or more when they wrap). */
    int heightForWidth(int width) const;
    /** Called when the chips now need a different height at the current width, so the host can re-lay itself out. */
    std::function<void()> onPreferredHeightChanged;

    void resized() override;

    /** The bands as shown, left to right and top to bottom (not counting the + chip). */
    std::vector<int> getChipBands() const;
    /** Chip of band `band`, or null; the + chip is addChip(). */
    juce::Button* getChip(int band) const;
    juce::Button* getAddChip() const;
    KeyedTween<juce::Rectangle<float>>& getGlideForTest() noexcept;

    /** Test mode: `chips` (each chip's label, left to right, "7 Bell"), `selectedBand` (1-based, 0 for none) and the
     * curve's `bandCount`, `soloBand`, `soloRing`, `othersDimmed`, `freqHz`, `gainDb`, `q`. */
    void addTestModeState(juce::DynamicObject& into) const override;

private:
    class Chip;
    struct Place {
        int key; // band index, or kAddKey
        juce::Rectangle<int> bounds;
    };
    static constexpr int kAddKey = ParametricEQModule::kMaxBands;
    static constexpr int kLeavingKeyBase = 1000;

    void eqCurveChanged() override;
    std::vector<int> shownBandsInOrder() const;
    std::vector<Place> layoutFor(int width) const;
    Chip* chipForKey(int key) const;
    void refresh();
    void placeChips(bool animate);
    void retireChip(int band, std::unique_ptr<Chip> chip);
    void pruneLeaving();
    void applyDrawnBounds();

    EQCurveComponent& curve_;
    std::map<int, std::unique_ptr<Chip>> chips_;
    std::unique_ptr<Chip> addChip_;
    std::map<int, std::unique_ptr<Chip>> leaving_;
    std::map<int, juce::Rectangle<float>> leavingRest_;
    std::map<int, juce::Rectangle<int>> resting_;
    struct Glide;
    std::unique_ptr<Glide> glide_;
    ArrowKeyNavigation arrows_;
    int lastHeight_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EQBandStrip)
};

} // namespace synth::ui
