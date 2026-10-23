// SoundBrowserShapes.cpp -- the Shapes place of the Sounds view: the library's Filter shapes and LFO shapes as rows
// with a small curve thumbnail, and dragging one out to a shape window chip or a card.
#include "UI/Layout/DragCursor.h"
#include "UI/Library/SoundBrowser/ShapeDragPayload.h"
#include "UI/Library/SoundBrowser/SoundBrowserComponent.h"
#include "UI/Library/SoundBrowser/SoundBrowserPackLayout.h"
#include "UI/Library/SoundBrowser/SoundDragState.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

namespace {
constexpr int kThumbWidth = 44;
constexpr int kThumbHeight = 18;
constexpr int kThumbGap = 10;
constexpr float kThumbLineWidth = 1.4f;

const char* groupTitle(synth::shapes::ShapeKind kind) {
    return kind == synth::shapes::ShapeKind::Filter ? "Filter shapes" : "LFO shapes";
}

juce::String kindName(synth::shapes::ShapeKind kind) {
    return kind == synth::shapes::ShapeKind::Filter ? "filter" : "LFO";
}
} // namespace

void SoundBrowserComponent::setShapeLibrary(synth::shapes::ShapeLibrary* library) {
    if (shapeLibrary_ == library)
        return;
    if (shapeLibrary_ != nullptr)
        shapeLibrary_->removeListener(this);
    shapeLibrary_ = library;
    if (shapeLibrary_ != nullptr)
        shapeLibrary_->addListener(this);
    refreshShapeCache();
    rebuildRows();
}

// The disk is read here and on a change, never while painting or building a place list.
void SoundBrowserComponent::refreshShapeCache() {
    filterShapes_.clear();
    lfoShapes_.clear();
    if (shapeLibrary_ == nullptr)
        return;
    filterShapes_ = shapeLibrary_->allShapes(synth::shapes::ShapeKind::Filter);
    lfoShapes_ = shapeLibrary_->allShapes(synth::shapes::ShapeKind::Lfo);
}

int SoundBrowserComponent::shapeCount() const { return (int)(filterShapes_.size() + lfoShapes_.size()); }

void SoundBrowserComponent::shapeLibraryChanged() {
    refreshShapeCache();
    rebuildRows();
}

// Two groups, each led by a heading; the library lists a kind's saved shapes before its built-in ones.
void SoundBrowserComponent::rebuildShapeRows() {
    for (const auto* group : {&filterShapes_, &lfoShapes_}) {
        if (group->empty())
            continue;
        rows_.push_back({RowKind::Hint, groupTitle(group->front().kind), {}, {}, {}, 0, std::nullopt});
        for (const auto& shape : *group) {
            Row row;
            row.kind = RowKind::Shape;
            row.text = shape.name;
            row.detail = shape.factory ? juce::String() : juce::String("yours");
            row.shape = shape;
            rows_.push_back(std::move(row));
        }
    }
    if (rows_.empty())
        rows_.push_back({RowKind::Hint, "No shapes yet", {}, {}, {}, 0, std::nullopt});
}

juce::String SoundBrowserComponent::shapeTooltip(const Row& row) const {
    return row.text + ", a " + kindName(row.shape->kind) + " shape. Drag it onto a " +
           (row.shape->kind == synth::shapes::ShapeKind::Filter ? "Filter's shape chip or Custom card"
                                                                : "LFO's shape chip or Custom card") +
           ". With the keyboard, use Start from in the shape window.";
}

// The curve drawn as a polyline across the thumbnail box, in the theme's accent.
void SoundBrowserComponent::paintShapeRow(juce::Graphics& g, const Row& row, juce::Rectangle<int> area) const {
    const auto& c = synth::theme::themeOf(*this).colors;
    const int side = kSoundRowSideMargin;
    const auto box = juce::Rectangle<float>((float)(area.getX() + side), (float)area.getCentreY() - kThumbHeight * 0.5f,
                                            (float)kThumbWidth, (float)kThumbHeight);
    g.setColour(c.bg1);
    g.fillRoundedRectangle(box, 3.0f);
    g.setColour(c.border);
    g.drawRoundedRectangle(box, 3.0f, 1.0f);
    juce::Path line;
    const auto inner = box.reduced(3.0f, 3.0f);
    for (int i = 0; i < kThumbWidth - 6; ++i) {
        const float x = (float)i / (float)(kThumbWidth - 7);
        const float y = inner.getBottom() - row.shape->curve.evaluate(x) * inner.getHeight();
        if (i == 0)
            line.startNewSubPath(inner.getX(), y);
        else
            line.lineTo(inner.getX() + x * inner.getWidth(), y);
    }
    g.setColour(c.accent);
    g.strokePath(line, juce::PathStrokeType(kThumbLineWidth));

    const juce::Font detailFont(juce::FontOptions(12.0f));
    const int detailWidth = row.detail.isEmpty() ? 0 : (int)std::ceil(detailFont.getStringWidthFloat(row.detail));
    const int nameLeft = area.getX() + side + kThumbWidth + kThumbGap;
    const int nameRight = area.getRight() - side - (detailWidth > 0 ? detailWidth + 8 : 0);
    g.setColour(c.textPrimary);
    g.setFont(juce::Font(juce::FontOptions(14.0f)));
    g.drawText(row.text, nameLeft, area.getY(), juce::jmax(0, nameRight - nameLeft), area.getHeight(),
               juce::Justification::centredLeft, true);
    if (detailWidth > 0) {
        g.setColour(c.textMuted);
        g.setFont(detailFont);
        g.drawText(row.detail, area.getRight() - side - detailWidth, area.getY(), detailWidth, area.getHeight(),
                   juce::Justification::centredRight);
    }
}

bool SoundBrowserComponent::beginShapeDrag(int index) {
    if (index < 0 || index >= (int)rows_.size() || rows_[(size_t)index].kind != RowKind::Shape)
        return false;
    const auto& row = rows_[(size_t)index];
    const ShapeRef ref{row.shape->kind, row.shape->factory, row.shape->name};
    // allowDraggingToOtherJuceWindows: the shape window that takes the drop is a window of its own.
    if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor(this))
        container->startDragging(makeShapePayload(ref), this, makeSoundDragImage(row.text), true);
    soundDragging_ = true;
    showDragCursor(*this);
    SoundDragState::beginShape(ref.kind, ref.name);
    return true;
}

} // namespace synth::ui
