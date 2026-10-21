// DrumRowLabels: the drum machine's row names with their Mute and Solo toggles. The component only draws and reports
// toggles; DrumMachineComponent owns the state (it lives on the clip) and pushes it back through setRows.

#include "DrumRowLabels.h"

#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <algorithm>

namespace synth::ui {

namespace {
constexpr int kDotDiameter = 8;
constexpr int kDotLeft = 8;
constexpr int kToggleWidth = 20;
constexpr int kToggleGap = 2;
constexpr int kRightPad = 4;
constexpr float kDimmedAlpha = 0.45f;
} // namespace

DrumRowLabels::Controls& DrumRowLabels::controlsFor(const DrumLane& lane) {
    auto& controls = controls_[lane.pitch];
    if (controls.mute != nullptr)
        return controls;

    controls.mute = std::make_unique<juce::TextButton>("M");
    controls.solo = std::make_unique<juce::TextButton>("S");
    const int pitch = lane.pitch;
    for (auto* button : {controls.mute.get(), controls.solo.get()}) {
        button->setClickingTogglesState(true);
        button->setMouseClickGrabsKeyboardFocus(false);
        addChildComponent(*button);
    }
    controls.mute->setComponentID("drumRowMute" + juce::String(pitch));
    controls.mute->setTitle("Mute " + lane.name);
    controls.mute->setTooltip("Mute " + lane.name + ": silence this drum without deleting its hits");
    controls.mute->onClick = [this, pitch] {
        if (onMuteToggled)
            onMuteToggled(pitch, controls_[pitch].mute->getToggleState());
    };
    controls.solo->setComponentID("drumRowSolo" + juce::String(pitch));
    controls.solo->setTitle("Solo " + lane.name);
    controls.solo->setTooltip("Solo " + lane.name + ": hear only the soloed drums");
    controls.solo->onClick = [this, pitch] {
        if (onSoloToggled)
            onSoloToggled(pitch, controls_[pitch].solo->getToggleState());
    };
    applyThemeColours();
    return controls;
}

void DrumRowLabels::setRows(std::vector<Row> rows) {
    rows_ = std::move(rows);
    for (const auto& row : rows_) {
        auto& controls = controlsFor(row.lane);
        controls.mute->setToggleState(row.muted, juce::dontSendNotification);
        controls.solo->setToggleState(row.soloed, juce::dontSendNotification);
    }
    // A row that is gone takes its buttons with it, so the Tab order never holds a hidden drum.
    for (auto it = controls_.begin(); it != controls_.end();) {
        const bool kept =
            std::any_of(rows_.begin(), rows_.end(), [&](const Row& r) { return r.lane.pitch == it->first; });
        it = kept ? std::next(it) : controls_.erase(it);
    }
    layoutControls();
    repaint();
}

juce::Button* DrumRowLabels::getMuteButton(int pitch) const {
    const auto it = controls_.find(pitch);
    return it == controls_.end() ? nullptr : it->second.mute.get();
}

juce::Button* DrumRowLabels::getSoloButton(int pitch) const {
    const auto it = controls_.find(pitch);
    return it == controls_.end() ? nullptr : it->second.solo.get();
}

void DrumRowLabels::layoutControls() {
    for (const auto& row : rows_) {
        auto& controls = controls_[row.lane.pitch];
        // A row mid-grow or mid-shrink has no room for buttons yet; they appear once it is whole.
        const bool whole = row.height >= 20 && row.alpha >= 0.999f;
        auto strip = juce::Rectangle<int>(0, row.top, getWidth(), row.height).withTrimmedRight(kRightPad);
        const int h = juce::jmin(18, row.height);
        const auto solo = strip.removeFromRight(kToggleWidth).withSizeKeepingCentre(kToggleWidth, h);
        strip.removeFromRight(kToggleGap);
        const auto mute = strip.removeFromRight(kToggleWidth).withSizeKeepingCentre(kToggleWidth, h);
        controls.mute->setBounds(mute);
        controls.solo->setBounds(solo);
        controls.mute->setVisible(whole);
        controls.solo->setVisible(whole);
    }
}

void DrumRowLabels::resized() { layoutControls(); }

void DrumRowLabels::applyThemeColours() {
    const auto& c = synth::theme::themeOf(*this).colors;
    for (auto& [pitch, controls] : controls_) {
        controls.mute->setColour(juce::TextButton::buttonOnColourId, c.trackMuteOn);
        controls.mute->setColour(juce::TextButton::textColourOnId, c.bg0);
        controls.solo->setColour(juce::TextButton::buttonOnColourId, c.trackSoloOn);
        controls.solo->setColour(juce::TextButton::textColourOnId, c.bg0);
    }
}

void DrumRowLabels::lookAndFeelChanged() { applyThemeColours(); }

void DrumRowLabels::paint(juce::Graphics& g) {
    const auto& theme = synth::theme::themeOf(*this);
    g.fillAll(theme.colors.surface);
    g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), theme.type.value + 1.0f, juce::Font::plain));
    for (const auto& row : rows_) {
        if (row.height <= 0)
            continue;
        const auto bounds = juce::Rectangle<int>(0, row.top, getWidth(), row.height);
        const float alpha = (row.dimmed ? kDimmedAlpha : 1.0f) * row.alpha;
        g.setColour(row.lane.colour.withAlpha(alpha));
        g.fillEllipse(juce::Rectangle<float>((float)kDotLeft, (float)bounds.getCentreY() - kDotDiameter * 0.5f,
                                             (float)kDotDiameter, (float)kDotDiameter));
        g.setColour(theme.colors.textPrimary.withAlpha(alpha));
        auto text = bounds.withTrimmedLeft(kDotLeft + kDotDiameter + 6)
                        .withTrimmedRight(2 * kToggleWidth + kToggleGap + kRightPad + 2);
        g.drawText(row.lane.name, text, juce::Justification::centredLeft, true);
    }
    g.setColour(theme.colors.border);
    g.drawVerticalLine(getWidth() - 1, 0.0f, (float)getHeight());
}

} // namespace synth::ui
