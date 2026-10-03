// Concern: the track header's automation fold arrow and the folded track's lane-count badge.
#include "UI/Timeline/AutomationLanes/TrackFoldArrow.h"

#include "UI/Layout/FocusRing.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <cmath>

namespace synth::ui {

namespace {
constexpr float kGlyphSize = 8.0f;
constexpr float kBadgeFontSize = 9.5f;
constexpr int kBadgePadding = 4;
} // namespace

// Focusable for the keyboard path, but a click must not pull focus off the row: the row's own
// focus is what its M/S/R keys and the panel's selected track follow.
TrackFoldArrow::TrackFoldArrow()
    : juce::Button("trackAutomationFoldArrow") {
    setComponentID("trackAutomationFoldArrow");
    setWantsKeyboardFocus(true);
    setMouseClickGrabsKeyboardFocus(false);
    setClickingTogglesState(false);
    setState(false, {});
}

// The name says what the next press does, so it is rebuilt on every state or rename.
void TrackFoldArrow::setState(bool expanded, const juce::String& trackName, int laneCount) {
    expanded_ = expanded;
    juce::String text = (expanded ? "Hide " : "Show ") + trackName + " automation";
    if (!expanded && laneCount > 0)
        text << " (" << laneCountBadgeText(laneCount) << ")";
    setTitle(text);
    setTooltip(text);
    repaint();
}

void TrackFoldArrow::paintButton(juce::Graphics& g, bool highlighted, bool) {
    const auto bounds = getLocalBounds().toFloat();
    const auto glyph = juce::Rectangle<float>(kGlyphSize, kGlyphSize).withCentre(bounds.getCentre());
    synth::theme::paintDisclosureChevron(g, glyph, expanded_ ? 1.0f : 0.0f, synth::theme::themeOf(*this), highlighted);

    synth::ui::paintFocusRing(g, bounds, *this, 3.0f);
}

// juce::Button fires onClick from any mouse button, so a popup click never reaches the base class:
// the matching mouseUp then finds the button not down and fires nothing.
void TrackFoldArrow::mouseDown(const juce::MouseEvent& e) {
    if (e.mods.isPopupMenu()) {
        if (onPopupMenuRequested)
            onPopupMenuRequested();
        return;
    }
    juce::Button::mouseDown(e);
}

// Return and Space toggle at once. juce::Button's own Return handling posts the click to the message
// queue, and it has no Space handling, so neither would answer the key in the same event.
bool TrackFoldArrow::keyPressed(const juce::KeyPress& key) {
    if (key.isKeyCode(juce::KeyPress::returnKey) || key.isKeyCode(juce::KeyPress::spaceKey)) {
        if (isEnabled() && onClick)
            onClick();
        return true;
    }
    return juce::Button::keyPressed(key);
}

juce::String laneCountBadgeText(int laneCount) {
    return juce::String(laneCount) + (laneCount == 1 ? " lane" : " lanes");
}

int laneCountBadgeWidth(int laneCount) {
    const juce::Font font{juce::FontOptions(kBadgeFontSize)};
    return (int)std::ceil(font.getStringWidthFloat(laneCountBadgeText(laneCount))) + 2 * kBadgePadding;
}

void paintLaneCountBadge(juce::Graphics& g, juce::Rectangle<int> area, int laneCount, const juce::Component& owner) {
    if (area.isEmpty() || laneCount <= 0)
        return;
    const auto& colors = synth::theme::themeOf(owner).colors;
    const auto muted = colors.textMuted;
    const auto border = colors.border;
    const auto pill = area.toFloat().reduced(0.5f);
    g.setColour(border);
    g.drawRoundedRectangle(pill, 3.0f, 1.0f);
    g.setColour(muted);
    g.setFont(juce::Font(juce::FontOptions(kBadgeFontSize)));
    g.drawText(laneCountBadgeText(laneCount), area, juce::Justification::centred, false);
}

} // namespace synth::ui
