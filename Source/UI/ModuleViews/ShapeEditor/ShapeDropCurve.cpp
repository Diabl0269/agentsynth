// ShapeDropCurve.cpp -- a library shape dropped on a curve editor, and the hint shown while one hovers.
#include "ShapeDropCurve.h"
#include "Modules/Shapes/ShapeLibrary.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

bool ShapeDropCurveEditor::acceptsShapeDrop(synth::shapes::ShapeKind kind) const {
    return dropHooks.accepts && dropHooks.accepts(kind) && dropHooks.targetName && dropHooks.targetName().isNotEmpty();
}

void ShapeDropCurveEditor::shapeDragOver(bool over, const juce::String& name) {
    dropOver_ = over;
    dropName_ = over ? name : juce::String();
    repaint();
}

void ShapeDropCurveEditor::shapeDropped(const synth::shapes::LibraryShape& shape) {
    if (dropHooks.dropped)
        dropHooks.dropped(shape);
}

juce::String ShapeDropCurveEditor::getDropHint() const {
    if (!dropOver_ || !dropHooks.targetName)
        return {};
    return "Drop to stamp " + dropName_ + " onto " + dropHooks.targetName();
}

/** The faint outline while a shape this editor takes is dragged anywhere; over it, the full outline and a label saying
    which dot's curve the drop replaces. */
void ShapeDropCurveEditor::paintOverChildren(juce::Graphics& g) {
    CurveEditorComponent::paintOverChildren(g);
    if (!dropOver_ && !isShowingSoundOutline())
        return;
    const auto& theme = synth::theme::themeOf(*this);
    const auto bounds = getLocalBounds().toFloat().reduced(1.0f);
    g.setColour(theme.colors.accent.withAlpha(dropOver_ ? 1.0f : 0.35f));
    g.drawRoundedRectangle(bounds, theme.metrics.cornerRadius, dropOver_ ? 2.0f : 1.0f);
    if (!dropOver_)
        return;
    g.setColour(theme.colors.accent.withAlpha(0.1f));
    g.fillRoundedRectangle(bounds, theme.metrics.cornerRadius);
    const juce::Font font{juce::FontOptions(13.0f)};
    const auto label = getDropHint();
    const float width =
        juce::jmin(bounds.getWidth() - 16.0f, juce::GlyphArrangement::getStringWidth(font, label) + 20.0f);
    const auto pill = juce::Rectangle<float>(width, 24.0f).withCentre(bounds.getCentre());
    g.setColour(theme.colors.surfaceHi.withAlpha(0.95f));
    g.fillRoundedRectangle(pill, theme.metrics.cornerRadiusSmall);
    g.setColour(theme.colors.textPrimary);
    g.setFont(font);
    g.drawText(label, pill.reduced(8.0f, 0.0f), juce::Justification::centred, true);
}

} // namespace synth::ui
