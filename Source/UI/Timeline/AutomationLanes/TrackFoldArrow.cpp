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

juce::Colour themed(const juce::Component& c, juce::Colour fallback, juce::Colour synth::theme::Colors::* token) {
    if (auto* lf = dynamic_cast<const synth::theme::AppLookAndFeel*>(&c.getLookAndFeel()))
        return lf->getTheme().colors.*token;
    return fallback;
}
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
    auto colour = themed(*this, juce::Colour(0xffEAEEF3), &synth::theme::Colors::textPrimary);
    if (!highlighted)
        colour = colour.withMultipliedAlpha(0.85f);

    // A filled triangle rather than a text glyph, so it never depends on a font's arrow coverage.
    const auto glyph = juce::Rectangle<float>(kGlyphSize, kGlyphSize).withCentre(bounds.getCentre());
    juce::Path arrow;
    if (expanded_)
        arrow.addTriangle(glyph.getTopLeft(), glyph.getTopRight(), {glyph.getCentreX(), glyph.getBottom() - 1.0f});
    else
        arrow.addTriangle(glyph.getTopLeft(), glyph.getBottomLeft(), {glyph.getRight() - 1.0f, glyph.getCentreY()});
    g.setColour(colour);
    g.fillPath(arrow);

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
    const auto muted = themed(owner, juce::Colour(0xff8A93A0), &synth::theme::Colors::textMuted);
    const auto border = themed(owner, juce::Colour(0xff2A2F38), &synth::theme::Colors::border);
    const auto pill = area.toFloat().reduced(0.5f);
    g.setColour(border);
    g.drawRoundedRectangle(pill, 3.0f, 1.0f);
    g.setColour(muted);
    g.setFont(juce::Font(juce::FontOptions(kBadgeFontSize)));
    g.drawText(laneCountBadgeText(laneCount), area, juce::Justification::centred, false);
}

} // namespace synth::ui
