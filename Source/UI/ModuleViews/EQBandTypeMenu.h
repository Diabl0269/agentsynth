#pragma once

#include "Modules/FX/ParametricEQ/ParametricEQBandTypes.h"
#include "UI/Layout/ArrowKeyNavigation.h"
#include "UI/Layout/PopoverContent.h"
#include <array>
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

namespace synth::ui {

/** The popover that picks a Parametric EQ band's type (the eight shapes) and, for a cut, its slope. Opened through
 *  launchPopover by EQCurveComponent; a type that is not a cut closes it, a cut keeps it open so the slope can follow.
 */
class EQBandTypeMenu
    : public juce::Component
    , public PopoverContent {
public:
    EQBandTypeMenu(synth::eq::BandType current, int slopeDbPerOct);
    ~EQBandTypeMenu() override;

    /** A type (slope 0) or a slope (the current type) was picked. */
    std::function<void(synth::eq::BandType, int)> onChoose;

    bool growsFromOpener() const override { return true; }
    void resized() override;
    void paint(juce::Graphics& g) override;
    void parentHierarchyChanged() override;

    /** Tests: press a row as a click would. */
    void chooseTypeForTest(synth::eq::BandType type);
    void chooseSlopeForTest(int slopeDbPerOct);
    juce::Component* typeRowForTest(synth::eq::BandType type);
    juce::Component* slopeRowForTest(int slopeDbPerOct);
    bool slopesEnabledForTest() const;

private:
    class Row;
    void chooseType(synth::eq::BandType type);
    void chooseSlope(int slopeDbPerOct);
    void showCurrent();

    synth::eq::BandType current_;
    int slope_;
    std::array<std::unique_ptr<Row>, synth::eq::kNumBandTypes> typeRows_;
    std::array<std::unique_ptr<Row>, 9> slopeRows_;
    ArrowKeyNavigation arrows_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EQBandTypeMenu)
};

/** True for the two types that have a slope. */
inline bool eqTypeHasSlope(synth::eq::BandType type) noexcept {
    return type == synth::eq::BandType::LowCut || type == synth::eq::BandType::HighCut;
}

} // namespace synth::ui
