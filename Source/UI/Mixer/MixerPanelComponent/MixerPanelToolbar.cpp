// Concern: MixerPanelToolbar's buttons -- the section toggles' state mirroring and the row layout.
#include "MixerPanelToolbar.h"

#include "UI/Layout/UIAnimation.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

namespace {
constexpr int kToggleGap = 2;
constexpr int kSideInset = 4;

MixerSection sectionAt(size_t index) { return (MixerSection)(int)index; }
constexpr synth::theme::Icon kSectionIcons[MixerSectionLayout::kSectionCount] = {
    synth::theme::Icon::MixerInserts, synth::theme::Icon::MixerSends, synth::theme::Icon::MixerEq};
} // namespace

// Every button is a ToolbarButton in its compact inline layout (a 24 x 20 chip with the name to its right, so the
// bar keeps its height); the section toggles light like the top bar's toggles. Every button is its own Tab stop that
// acts on Return and Space; the focused column's keys stay with the panel because a key a button does not use bubbles
// up to it.
MixerPanelToolbar::MixerPanelToolbar() {
    setWantsKeyboardFocus(false);
    for (size_t i = 0; i < toggles_.size(); ++i) {
        auto& toggle = toggles_[i];
        toggle.setIcon(kSectionIcons[i], ToolbarGroup::View);
        toggle.setClickingTogglesState(false); // the layout is the source of truth, see refresh()
        toggle.setWantsKeyboardFocus(true);
        toggle.onClick = [this, i] {
            if (layout_ != nullptr)
                layout_->toggleHidden(sectionAt(i));
        };
        addAndMakeVisible(toggle);
    }
    addAndMakeVisible(paneButton_);
    for (auto* button : {&resetMetersButton_, &addBusButton_}) {
        button->setClickingTogglesState(false);
        button->setWantsKeyboardFocus(true);
        addAndMakeVisible(*button);
    }
    resetMetersButton_.setIcon(synth::theme::Icon::MixerResetMeters, ToolbarGroup::Housekeeping);
    resetMetersButton_.setTooltip("Reset Meters: clear every channel's peak readout to -inf");
    addBusButton_.setIcon(synth::theme::Icon::MixerAddBus, ToolbarGroup::Edit);
    addBusButton_.setTooltip("Add Bus: a bus channel (a group or send target)");
    setLabelsShown(true);
    resetMetersButton_.onClick = [this] {
        if (onResetMeters)
            onResetMeters();
    };
    addBusButton_.onClick = [this] {
        if (onAddBus)
            onAddBus();
    };
}

void MixerPanelToolbar::setLabelsShown(bool shown) {
    labelsShown_ = shown;
    for (size_t i = 0; i < toggles_.size(); ++i) {
        toggles_[i].setLayout(ToolbarButtonLayout::InlineCompact);
        toggles_[i].setLabel(MixerSectionLayout::nameOf(sectionAt(i)), shown);
    }
    for (auto* button : {&resetMetersButton_, &addBusButton_}) {
        button->setLayout(ToolbarButtonLayout::InlineCompact);
        button->setLabel(button->getName(), shown);
    }
    resized();
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
        const auto base = juce::String(hidden ? "Show " : "Hide ") + MixerSectionLayout::nameOf(sectionAt(i));
        toggles_[i].setTooltip(
            formatShortcutHint(base, shortcutTextFor ? shortcutTextFor(sectionAt(i)) : juce::String()));
    }
}

void MixerPanelToolbar::resized() {
    auto area = getLocalBounds().withTrimmedBottom(1).reduced(kSideInset, 0);
    if (paneButton_.isVisible()) {
        paneButton_.setBounds(area.removeFromLeft(SidePaneToggleButton::kWidth));
        area.removeFromLeft(kToggleGap * 2);
    }
    for (size_t i = 0; i < toggles_.size(); ++i) {
        toggles_[i].setBounds(area.removeFromLeft(toggles_[i].getPreferredWidth()));
        area.removeFromLeft(kToggleGap);
    }
    if (dockBack_ != nullptr) {
        dockBack_->setBounds(area.removeFromRight(DockBackSlot::kButtonWidth).reduced(0, 1));
        area.removeFromRight(kToggleGap * 2);
    }
    addBusButton_.setBounds(area.removeFromRight(addBusButton_.getPreferredWidth()).reduced(0, 1));
    area.removeFromRight(kToggleGap);
    resetMetersButton_.setBounds(area.removeFromRight(resetMetersButton_.getPreferredWidth()).reduced(0, 1));
}

void MixerPanelToolbar::paint(juce::Graphics& g) {
    g.fillAll(synth::theme::themeOf(*this).colors.bg0);
    g.setColour(synth::theme::themeOf(*this).colors.border);
    g.fillRect(0, getHeight() - 1, getWidth(), 1);
}

} // namespace synth::ui
