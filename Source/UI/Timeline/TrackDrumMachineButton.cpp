// Concern: the Drum Kit track header's "Drum machine" button.
#include "UI/Timeline/TrackDrumMachineButton.h"

#include "UI/Layout/FocusRing.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

namespace {
constexpr float kGlyphSize = 12.0f;
constexpr float kCellSize = 3.0f;
constexpr float kCellGap = 1.5f;
constexpr int kColumns = 3;
constexpr int kRows = 3;
} // namespace

TrackDrumMachineButton::TrackDrumMachineButton() {
    setComponentID("trackDrumMachineButton");
    setTexts("Drum machine", "Open the drum machine on the clip under the playhead");
}

// Three rows of three steps with a few lit: a pattern, drawn as rectangles in the theme's text colours so it follows
// every theme with no icon asset.
void TrackDrumMachineButton::paintButton(juce::Graphics& g, bool highlighted, bool down) {
    const auto bounds = getLocalBounds().toFloat();
    const auto& colors = synth::theme::themeOf(*this).colors;
    if (highlighted || down) {
        g.setColour(colors.textPrimary.withAlpha(down ? 0.18f : 0.1f));
        g.fillRoundedRectangle(bounds, 3.0f);
    }
    static constexpr bool kLit[kRows][kColumns] = {{true, false, true}, {false, true, false}, {true, false, false}};
    const float pitch = kCellSize + kCellGap;
    const float side = pitch * (float)kColumns - kCellGap;
    const auto origin = juce::Rectangle<float>(side, side).withCentre(bounds.getCentre()).getPosition();
    for (int row = 0; row < kRows; ++row)
        for (int col = 0; col < kColumns; ++col) {
            const juce::Rectangle<float> cell(origin.x + (float)col * pitch, origin.y + (float)row * pitch, kCellSize,
                                              kCellSize);
            g.setColour(kLit[row][col] ? (highlighted ? colors.textPrimary : colors.textMuted)
                                       : colors.textMuted.withAlpha(0.35f));
            g.fillRoundedRectangle(cell, 0.8f);
        }
    synth::ui::paintFocusRing(g, bounds, *this, 3.0f);
}

} // namespace synth::ui
