// Concern: the centre line and scale labels an automation lane whose range straddles 0 is drawn with, so
// "above the line" and "below the line" read as the two directions (an amount lane: below = inverted).
#include "UI/Timeline/AutomationLanes/AutomationLaneBipolarGuide.h"

#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <cmath>

namespace synth::ui {

namespace {
constexpr float kLabelFontSize = 8.5f;
constexpr int kLabelWidth = 40;
constexpr int kLabelHeight = 11;
constexpr int kLabelInset = 3;
const float kDash[] = {4.0f, 3.0f};

bool isUnitRange(float minValue, float maxValue) noexcept {
    return std::abs(minValue + 1.0f) < 1.0e-6f && std::abs(maxValue - 1.0f) < 1.0e-6f;
}
} // namespace

bool isBipolarRange(float minValue, float maxValue) noexcept { return minValue < 0.0f && maxValue > 0.0f; }

void paintBipolarGuide(juce::Graphics& g, const juce::Component& lane, float minValue, float maxValue, float zeroY) {
    if (!isBipolarRange(minValue, maxValue))
        return;
    juce::Colour muted{0xff8A93A0};
    if (auto* lf = dynamic_cast<const synth::theme::AppLookAndFeel*>(&lane.getLookAndFeel()))
        muted = lf->getTheme().colors.textMuted;

    const float width = (float)lane.getWidth();
    g.setColour(muted.withAlpha(0.7f));
    g.drawDashedLine(juce::Line<float>(0.0f, zeroY, width, zeroY), kDash, 2, 1.0f);

    if (!isUnitRange(minValue, maxValue) || lane.getHeight() < 3 * kLabelHeight)
        return;
    g.setFont(juce::Font(juce::FontOptions(kLabelFontSize)));
    g.setColour(muted);
    const int height = lane.getHeight();
    const auto label = [&g](const juce::String& text, int y) {
        g.drawText(text, kLabelInset, y, kLabelWidth, kLabelHeight, juce::Justification::centredLeft, false);
    };
    label("+100%", 1);
    label("0", juce::jlimit(0, height - kLabelHeight, juce::roundToInt(zeroY) - kLabelHeight));
    label(juce::String::fromUTF8("\xE2\x88\x92") + "100%", height - kLabelHeight - 1);
}

} // namespace synth::ui
