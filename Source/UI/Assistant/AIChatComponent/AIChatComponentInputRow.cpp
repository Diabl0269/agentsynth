#include "AIChatComponent.h"
#include "UI/Layout/FadeVisibility.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/Layout/UIAnimation.h"

namespace synth {

// Concern: the request field wraps its text and the input row grows with it, from one line (the row's resting
// height) to kInputRowMaxLines, then scrolls inside the box. The row height is a float that eases between two
// whole-line heights on the shared AnimationDriver; resized() reads it, so the message list above gives up the same
// height on every frame.

int AIChatComponent::currentInputRowHeight() const { return juce::roundToInt(inputRowHeight_); }

// The text is centred in a one-line row and keeps the same padding above and below at every line count.
void AIChatComponent::applyInputIndents() {
    const int lineHeight = juce::roundToInt(inputField.getFont().getHeight());
    const int border = inputField.getBorder().getTop();
    inputField.setIndents(inputField.getLeftIndent(),
                          juce::jmax(0, (kInputRowMinHeight - 2 * border - lineHeight) / 2));
}

// The line count is measured here rather than read from the editor: a multi-line TextEditor never reports a text
// height below its own viewport, so its figure would only ever follow the row it is meant to size.
int AIChatComponent::inputRowHeightForText() const {
    const juce::Font font = inputField.getFont();
    const float lineHeight = font.getHeight();
    constexpr float kWrapSlack = 4.0f; // keeps the measure from wrapping later than the editor does
    const float wrapWidth =
        juce::jmax(20.0f, static_cast<float>(inputField.getWidth() - 2 * inputField.getBorder().getLeft() -
                                             inputField.getLeftIndent()) -
                              kWrapSlack);
    int lines = 0;
    for (const auto& paragraph : juce::StringArray::fromLines(inputField.getText().replace("\r", ""))) {
        juce::GlyphArrangement glyphs;
        glyphs.addJustifiedText(font, paragraph, 0.0f, 0.0f, wrapWidth, juce::Justification::left);
        lines += juce::jmax(1, juce::roundToInt(glyphs.getBoundingBox(0, -1, true).getHeight() / lineHeight));
    }
    if (inputField.getText().endsWithChar('\n'))
        ++lines; // fromLines drops the empty line after a trailing newline
    lines = juce::jlimit(1, kInputRowMaxLines, lines);
    return kInputRowMinHeight + juce::roundToInt(static_cast<float>(lines - 1) * lineHeight);
}

void AIChatComponent::updateInputRowHeight(bool fromLayout) {
    applyInputIndents();
    if (fromLayout && inputGrowAnim_.isRunning())
        return; // the running ease already heads for the row's height; a width change waits for the next edit
    const int target = inputRowHeightForText();
    if (target == inputRowTarget_)
        return;
    inputRowTarget_ = target;

    if (fromLayout || !synth::ui::FadeVisibility::canAnimateIn(this)) {
        inputGrowAnim_.stop(vblankUpdater);
        inputRowHeight_ = static_cast<float>(target);
        resized();
        return;
    }

    const float from = inputRowHeight_;
    const float to = static_cast<float>(target);
    // A short ease like the other height changes in this panel; under Reduce Motion a brief plain slide.
    const bool reduced = synth::ui::prefersReducedMotion();
    inputGrowAnim_.start(
        vblankUpdater, reduced ? 80.0 : 140.0, reduced ? [](float t) { return t; } : synth::ui::easeOutCubic,
        [this, from, to](float t) {
            inputRowHeight_ = from + (to - from) * t;
            resized();
        },
        [this, to] {
            inputRowHeight_ = to;
            resized();
            // The box now shows every line it can: bring the caret back into view.
            inputField.setCaretPosition(inputField.getCaretPosition());
        });
}

void AIChatComponent::textEditorTextChanged(juce::TextEditor& editor) {
    if (&editor == &inputField)
        updateInputRowHeight(false);
}

} // namespace synth
