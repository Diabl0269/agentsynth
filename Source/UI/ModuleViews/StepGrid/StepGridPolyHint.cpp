// StepGridPolyHint.cpp -- the "plays one note at a time" line and its Turn Poly on link. The text is muted ink, the
// link is the shared TextLinkButton, so the focus ring, hover and colours come from the theme.
#include "StepGridPolyHint.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

namespace {
constexpr float kFont = 11.0f;
constexpr int kGap = 6;
const char* const kLinkText = "Turn Poly on";
} // namespace

StepGridPolyHint::StepGridPolyHint()
    : link_(kLinkText, juce::Justification::centredLeft) {
    setComponentID("polyHint");
    setTitle("Poly hint");
    link_.setComponentID("polyHintLink");
    setInterceptsMouseClicks(false, true);
    link_.setTooltip("Switch the voices after this sequencer to Poly so every note of a chord plays");
    link_.setWantsKeyboardFocus(true);
    link_.onClick = [this] {
        if (onTurnPolyOn)
            onTurnPolyOn();
    };
    addAndMakeVisible(link_);
    setVisible(false);
}

StepGridPolyHint::~StepGridPolyHint() = default;

juce::String StepGridPolyHint::lineFor(const juce::String& moduleName) {
    return "Next: " + moduleName + " plays one note at a time";
}

void StepGridPolyHint::setModule(const juce::String& moduleName) {
    if (moduleName == module_)
        return;
    module_ = moduleName;
    const auto line = lineFor(module_);
    setTooltip(line);
    link_.setTitle(module_.isEmpty() ? juce::String(kLinkText) : "Turn Poly on for " + module_);
    setVisible(module_.isNotEmpty());
    resized();
    repaint();
}

int StepGridPolyHint::preferredWidth() const {
    return theme::AppLookAndFeel::uiTextWidth(lineFor(module_), kFont) + kGap +
           theme::AppLookAndFeel::uiTextWidth(kLinkText, kFont) + 4;
}

void StepGridPolyHint::resized() {
    const int linkWidth = theme::AppLookAndFeel::uiTextWidth(kLinkText, kFont) + 4;
    link_.setBounds(getWidth() - linkWidth, 0, linkWidth, getHeight());
}

void StepGridPolyHint::paint(juce::Graphics& g) {
    g.setColour(theme::themeOf(*this).colors.textMuted);
    g.setFont(theme::AppLookAndFeel::uiFont(kFont));
    g.drawText(lineFor(module_), 0, 0, std::max(0, link_.getX() - kGap), getHeight(), juce::Justification::centredLeft,
               true);
}

} // namespace synth::ui
