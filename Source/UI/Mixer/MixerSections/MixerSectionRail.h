#pragma once

#include "MixerSectionControls.h"
#include "MixerSectionLayout.h"
#include <array>
#include <juce_gui_basics/juce_gui_basics.h>

// MixerSectionRail.h (docs/mixer/panel.md#shared-sections): the fixed strip on the mixer panel's left
// edge that names each shared section and holds its show/hide chevron and its divider grip. It sits
// outside the panel's horizontally scrolling viewport, so it stays put while columns scroll sideways.
namespace synth::ui {

class MixerSectionRail : public juce::Component {
public:
    static constexpr int kWidth = 74;

    MixerSectionRail();

    /** `layout` must outlive this rail. */
    void setLayout(MixerSectionLayout& layout);
    /** The columns' height and the panel's vertical scroll offset, so rail rows line up with them. */
    void setColumnGeometry(int columnHeight, int scrollY);
    /** Re-positions the rows after the shared layout changed. */
    void refreshLayout();

    juce::Button& getToggleButtonForTest(MixerSection section) noexcept;
    MixerSectionDivider& getDividerForTest(MixerSection section) noexcept;
    /** The drag bubble's text ("5 rows"), empty while no divider is being dragged. */
    juce::String getDragBubbleTextForTest() const;

    void paint(juce::Graphics& g) override;
    void paintOverChildren(juce::Graphics& g) override;
    void resized() override;

private:
    class ToggleButton : public juce::Button {
    public:
        ToggleButton();
        void setSection(MixerSectionLayout* layout, MixerSection section);
        void paintButton(juce::Graphics& g, bool highlighted, bool down) override;

    private:
        MixerSectionLayout* layout_ = nullptr;
        MixerSection section_ = MixerSection::Inserts;
    };

    juce::String dragBubbleText() const;

    MixerSectionLayout* layout_ = nullptr;
    int columnHeight_ = 0;
    int scrollY_ = 0;
    std::array<ToggleButton, MixerSectionLayout::kSectionCount> toggles_;
    std::array<MixerSectionDivider, MixerSectionLayout::kSectionCount> dividers_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixerSectionRail)
};

} // namespace synth::ui
