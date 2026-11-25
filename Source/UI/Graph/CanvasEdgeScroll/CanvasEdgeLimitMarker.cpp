#include "UI/Graph/CanvasEdgeScroll/CanvasEdgeLimitMarker.h"

#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

namespace {
constexpr float kLineWidth = 2.0f;
constexpr float kStripWidth = 24.0f;
constexpr float kFontSize = 12.0f;
constexpr float kChipPadX = 10.0f;
constexpr float kChipHeight = 22.0f;
constexpr float kChipInset = 10.0f; // from the view edge, past the line
constexpr double kFadeMs = 140.0;
const char* const kLabel = "Edge of canvas";

juce::String sideNames(unsigned sides) {
    juce::StringArray names;
    if (sides & CanvasEdgeLimitMarker::Left)
        names.add("left");
    if (sides & CanvasEdgeLimitMarker::Top)
        names.add("top");
    if (sides & CanvasEdgeLimitMarker::Right)
        names.add("right");
    if (sides & CanvasEdgeLimitMarker::Bottom)
        names.add("bottom");
    return names.joinIntoString(",");
}
} // namespace

// Decorative: no clicks, no focus, hidden from the screen reader; the drag it annotates is the control. Part of the
// fade-out keeps drawing the sides it last showed (`painted_`).
CanvasEdgeLimitMarker::CanvasEdgeLimitMarker(juce::Component& view)
    : view_(view)
    , fade_(*this) {
    setComponentID("canvasEdgeLimitMarker");
    setInterceptsMouseClicks(false, false);
    setWantsKeyboardFocus(false);
    setAccessible(false);
    setOpaque(false);
    fade_.onFrame = [this] {
        const bool visible = fade_.alpha() > 0.0f;
        if (!visible && sides_ == 0)
            painted_ = beyondPainted_ = 0;
        setVisible(visible);
        repaint();
    };
}

void CanvasEdgeLimitMarker::setSides(unsigned sides, unsigned beyond, juce::Rectangle<float> frame) {
    beyond &= sides;
    if (beyond != 0 && (frame != frame_ || beyond != beyond_)) {
        frame_ = frame; // the frame moves in the view as it pans: the line follows it
        repaint();
    }
    beyond_ = beyond;
    if (sides == sides_)
        return;
    sides_ = sides;
    if (sides != 0) {
        painted_ |= sides;
        beyondPainted_ |= beyond;
        setBounds(view_.getLocalBounds());
        setVisible(true);
        toFront(false);
    }
    fade_.fadeTo(sides != 0 ? 1.0f : 0.0f, kFadeMs, easeOutCubic);
}

void CanvasEdgeLimitMarker::addTestModeState(juce::DynamicObject& into) const {
    into.setProperty("sides", sideNames(sides_));
    into.setProperty("left", (sides_ & Left) != 0);
    into.setProperty("top", (sides_ & Top) != 0);
    into.setProperty("right", (sides_ & Right) != 0);
    into.setProperty("bottom", (sides_ & Bottom) != 0);
    into.setProperty("pastFrame", sideNames(beyond_));
}

void CanvasEdgeLimitMarker::paint(juce::Graphics& g) {
    const float alpha = fade_.alpha();
    if (alpha <= 0.0f || painted_ == 0)
        return;
    const auto& colors = synth::theme::themeOf(*this).colors;
    const auto viewArea = getLocalBounds().toFloat();
    const auto font = juce::Font(juce::FontOptions(kFontSize));
    const float chipW = juce::GlyphArrangement::getStringWidth(font, kLabel) + 2.0f * kChipPadX;

    const auto paintSide = [&](Side side) {
        if ((painted_ & side) == 0)
            return;
        // A side past the canvas frame is drawn along the frame's border, over the part of it the view shows.
        const auto area = (beyondPainted_ & side) != 0 ? frame_.getIntersection(viewArea) : viewArea;
        if (area.isEmpty())
            return;
        const auto centre = area.getCentre();
        juce::Rectangle<float> line, strip, chip(chipW, kChipHeight);
        juce::Point<float> from, to; // the strip's gradient runs from the edge inward
        switch (side) {
        case Left:
            line = area.withWidth(kLineWidth);
            strip = area.withWidth(kStripWidth);
            from = strip.getTopLeft();
            to = strip.getTopRight();
            chip.setPosition(kLineWidth + kChipInset, centre.y - kChipHeight / 2);
            break;
        case Right:
            line = area.withTrimmedLeft(area.getWidth() - kLineWidth);
            strip = area.withTrimmedLeft(area.getWidth() - kStripWidth);
            from = strip.getTopRight();
            to = strip.getTopLeft();
            chip.setPosition(area.getRight() - kLineWidth - kChipInset - chipW, centre.y - kChipHeight / 2);
            break;
        case Top:
            line = area.withHeight(kLineWidth);
            strip = area.withHeight(kStripWidth);
            from = strip.getTopLeft();
            to = strip.getBottomLeft();
            chip.setPosition(centre.x - chipW / 2, kLineWidth + kChipInset);
            break;
        default:
            line = area.withTrimmedTop(area.getHeight() - kLineWidth);
            strip = area.withTrimmedTop(area.getHeight() - kStripWidth);
            from = strip.getBottomLeft();
            to = strip.getTopLeft();
            chip.setPosition(centre.x - chipW / 2, area.getBottom() - kLineWidth - kChipInset - kChipHeight);
            break;
        }
        g.setGradientFill(juce::ColourGradient(colors.accent.withAlpha(0.22f * alpha), from,
                                               colors.accent.withAlpha(0.0f), to, false));
        g.fillRect(strip);
        g.setColour(colors.accent.withAlpha(alpha));
        g.fillRect(line);
        g.setColour(colors.surfaceHi.withAlpha(0.95f * alpha));
        g.fillRoundedRectangle(chip, kChipHeight / 2);
        g.setColour(colors.accent.withAlpha(alpha));
        g.drawRoundedRectangle(chip.reduced(0.5f), kChipHeight / 2, 1.0f);
        g.setColour(colors.textPrimary.withAlpha(alpha));
        g.setFont(font);
        g.drawText(kLabel, chip, juce::Justification::centred, false);
    };
    paintSide(Left);
    paintSide(Top);
    paintSide(Right);
    paintSide(Bottom);
}

} // namespace synth::ui
