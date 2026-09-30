// Concern: MixerPanelToolbar's buttons -- the section toggles' state mirroring and the row layout.
#include "MixerPanelToolbar.h"

#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

namespace {
constexpr int kToggleWidths[MixerSectionLayout::kSectionCount] = {60, 54, 40};
constexpr int kToggleGap = 2;
constexpr int kSideInset = 4;
constexpr int kResetMetersWidth = 84;
constexpr int kAddBusWidth = 54;

MixerSection sectionAt(size_t index) { return (MixerSection)(int)index; }
} // namespace

// The toggles are label-only DrawableButtons so the look-and-feel paints them with the same accent
// wash as the main toolbar's toggles. None of the buttons take keyboard focus: the panel stays the
// mixer's single focusable leaf.
MixerPanelToolbar::MixerPanelToolbar() {
    setWantsKeyboardFocus(false);
    for (size_t i = 0; i < toggles_.size(); ++i) {
        auto& toggle = toggles_[i];
        toggle.setButtonText(MixerSectionLayout::nameOf(sectionAt(i)));
        toggle.setTitle(MixerSectionLayout::nameOf(sectionAt(i)));
        toggle.setClickingTogglesState(false); // the layout is the source of truth, see refresh()
        toggle.setWantsKeyboardFocus(false);
        toggle.onClick = [this, i] {
            if (layout_ != nullptr)
                layout_->toggleHidden(sectionAt(i));
        };
        addAndMakeVisible(toggle);
    }
    addAndMakeVisible(paneButton_);
    for (auto* button : {&resetMetersButton_, &addBusButton_}) {
        button->setClickingTogglesState(false);
        button->setWantsKeyboardFocus(false);
        addAndMakeVisible(*button);
    }
    resetMetersButton_.onClick = [this] {
        if (onResetMeters)
            onResetMeters();
    };
    addBusButton_.onClick = [this] {
        if (onAddBus)
            onAddBus();
    };
}

void MixerPanelToolbar::setLayout(MixerSectionLayout& layout) {
    layout_ = &layout;
    refresh();
}

void MixerPanelToolbar::bindSidePane(SidePane& pane) {
    paneButton_.bind(&pane);
    resized();
}

void MixerPanelToolbar::refresh() {
    for (size_t i = 0; i < toggles_.size(); ++i) {
        const bool hidden = layout_ != nullptr && layout_->isHidden(sectionAt(i));
        toggles_[i].setToggleState(!hidden, juce::dontSendNotification);
        toggles_[i].setTooltip(juce::String(hidden ? "Show " : "Hide ") + MixerSectionLayout::nameOf(sectionAt(i)));
    }
}

void MixerPanelToolbar::resized() {
    auto area = getLocalBounds().withTrimmedBottom(1).reduced(kSideInset, 0);
    if (paneButton_.isVisible()) {
        paneButton_.setBounds(area.removeFromLeft(SidePaneToggleButton::kWidth));
        area.removeFromLeft(kToggleGap * 2);
    }
    for (size_t i = 0; i < toggles_.size(); ++i) {
        toggles_[i].setBounds(area.removeFromLeft(kToggleWidths[i]));
        area.removeFromLeft(kToggleGap);
    }
    addBusButton_.setBounds(area.removeFromRight(kAddBusWidth).reduced(0, 1));
    area.removeFromRight(kToggleGap);
    resetMetersButton_.setBounds(area.removeFromRight(kResetMetersWidth).reduced(0, 1));
}

void MixerPanelToolbar::paint(juce::Graphics& g) {
    const auto* laf = dynamic_cast<const synth::theme::AppLookAndFeel*>(&getLookAndFeel());
    g.fillAll(laf != nullptr ? laf->getTheme().colors.bg0 : juce::Colour(0xff0B0D10));
    g.setColour(laf != nullptr ? laf->getTheme().colors.border : juce::Colour(0xff2A2F38));
    g.fillRect(0, getHeight() - 1, getWidth(), 1);
}

} // namespace synth::ui
