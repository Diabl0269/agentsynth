// ShapeWindowMap.cpp -- a library shape dropped on the shape window's map, and the hint shown while one hovers.
#include "ShapeWindowMap.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

namespace {
constexpr float kRingRadius = 11.0f;
} // namespace

// The base class offers a shape's enter, exit and drop but not where the pointer is; the drop needs that.
void ShapeWindowMap::itemDragEnter(const SourceDetails& details) {
    trackPointer(details.localPosition);
    SoundDropAdapter::itemDragEnter(details);
}

void ShapeWindowMap::itemDragMove(const SourceDetails& details) {
    trackPointer(details.localPosition);
    SoundDropAdapter::itemDragMove(details);
}

void ShapeWindowMap::itemDropped(const SourceDetails& details) {
    trackPointer(details.localPosition);
    SoundDropAdapter::itemDropped(details);
}

void ShapeWindowMap::trackPointer(juce::Point<int> local) {
    const int dot = dotAt(local.toFloat());
    if (local == dropPoint_ && dot == dropDot_)
        return;
    dropPoint_ = local;
    dropDot_ = dot;
    if (dropOver_)
        repaint();
}

bool ShapeWindowMap::acceptsShapeDrop(synth::shapes::ShapeKind kind) const {
    return dropHooks.accepts && dropHooks.accepts(kind);
}

void ShapeWindowMap::shapeDragOver(bool over, const juce::String& name) {
    dropOver_ = over;
    dropName_ = over ? name : juce::String();
    repaint();
}

void ShapeWindowMap::shapeDropped(const synth::shapes::LibraryShape& shape) {
    if (!dropHooks.dropped)
        return;
    const int dot = dotAt(dropPoint_.toFloat());
    const auto at = toMap(dropPoint_.toFloat());
    dropHooks.dropped(shape, dot, {juce::jlimit(0.0f, 1.0f, at.x), juce::jlimit(0.0f, 1.0f, at.y)});
}

juce::String ShapeWindowMap::getDropHint() const {
    if (!dropOver_)
        return {};
    if (dropDot_ >= 0 && dropDot_ < getNumDots())
        return "Drop to stamp " + dropName_ + " onto " + getDots()[(size_t)dropDot_].name;
    return "Drop to add " + dropName_ + " here";
}

/** The faint outline while a shape this map takes is dragged anywhere; over the map, a ring on the dot it would
    stamp or a ghost dot where it would add one, and a label saying which. */
void ShapeWindowMap::paintOverChildren(juce::Graphics& g) {
    MorphMap::paintOverChildren(g);
    if (!dropOver_ && !isShowingSoundOutline())
        return;
    const auto& theme = synth::theme::themeOf(*this);
    const auto bounds = getLocalBounds().toFloat().reduced(1.0f);
    g.setColour(theme.colors.accent.withAlpha(dropOver_ ? 1.0f : 0.35f));
    g.drawRoundedRectangle(bounds, theme.metrics.cornerRadius, dropOver_ ? 2.0f : 1.0f);
    if (!dropOver_)
        return;
    const auto centre = dropDot_ >= 0 ? getDotCentre(dropDot_) : dropPoint_.toFloat();
    const auto ring = juce::Rectangle<float>(kRingRadius * 2.0f, kRingRadius * 2.0f).withCentre(centre);
    if (dropDot_ < 0) {
        g.setColour(theme.colors.accent.withAlpha(0.3f));
        g.fillEllipse(ring.reduced(4.0f));
    }
    g.setColour(theme.colors.accent);
    g.drawEllipse(ring, 2.0f);

    const juce::Font font{juce::FontOptions(13.0f)};
    const auto label = getDropHint();
    const float width =
        juce::jmin(bounds.getWidth() - 16.0f, juce::GlyphArrangement::getStringWidth(font, label) + 20.0f);
    auto pill = juce::Rectangle<float>(width, 24.0f).withCentre({bounds.getCentreX(), bounds.getY() + 22.0f});
    if (pill.expanded(4.0f).contains(centre))
        pill.setY(bounds.getBottom() - 34.0f);
    g.setColour(theme.colors.surfaceHi.withAlpha(0.95f));
    g.fillRoundedRectangle(pill, theme.metrics.cornerRadiusSmall);
    g.setColour(theme.colors.textPrimary);
    g.setFont(font);
    g.drawText(label, pill.reduced(8.0f, 0.0f), juce::Justification::centred, true);
}

} // namespace synth::ui
