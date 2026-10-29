// SoundBrowserShapes.cpp -- the Shapes place of the Sounds view: the library's Filter shapes and LFO shapes as rows
// with a small curve thumbnail, shapes in Favourites and Used recently, and dragging one out to a shape window chip or
// a card. Deleting, renaming and Return are in SoundBrowserShapeActions.cpp.
#include "SoundBrowser/SoundSearch.h"
#include "UI/Layout/DragCursor.h"
#include "UI/Library/SoundBrowser/ShapeCurveView.h"
#include "UI/Library/SoundBrowser/ShapeDragPayload.h"
#include "UI/Library/SoundBrowser/SoundBrowserComponent.h"
#include "UI/Library/SoundBrowser/SoundBrowserPackLayout.h"
#include "UI/Library/SoundBrowser/SoundBrowserText.h"
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

bool SoundBrowserComponent::onShapeLibraryRowsShowing() const {
    return !isSearchActive() && (placeId_ == kShapesId || placeId_ == kFavouritesId || placeId_ == kRecentId);
}

// A saved or deleted shape changes the rows of the places that list shapes; those fade from the old list to the new
// one, as a search does.
void SoundBrowserComponent::shapeLibraryChanged() {
    const bool animate = onShapeLibraryRowsShowing();
    if (animate)
        capturePreviousList();
    refreshShapeCache();
    rebuildRows();
    if (animate)
        playListMotion(0);
}

// A stamped shape goes to the front of Used recently; the Places screen shows the new count.
void SoundBrowserComponent::shapeLibraryUsed(const synth::shapes::LibraryShape& shape) {
    lists_.noteUsed(synth::shapes::shapeId(shape.kind, shape.factory, shape.name));
    if (placeId_.isEmpty() || placeId_ == kRecentId)
        rebuildRows();
}

const synth::shapes::LibraryShape* SoundBrowserComponent::cachedShape(synth::shapes::ShapeKind kind, bool factory,
                                                                      const juce::String& name) const {
    for (const auto& shape : kind == synth::shapes::ShapeKind::Filter ? filterShapes_ : lfoShapes_)
        if (shape.factory == factory && shape.name.equalsIgnoreCase(name))
            return &shape;
    return nullptr;
}

// A list that mixes shapes with the sounds (Favourites, Used recently) names the kind in the right column; a shape
// whose file is gone, and every shape while a filter is on (filters narrow sounds), is left out.
SoundBrowserComponent::Row SoundBrowserComponent::makeShapeRow(const synth::shapes::LibraryShape& shape,
                                                               bool inMixedList) const {
    Row row;
    row.kind = RowKind::Shape;
    row.text = shape.name;
    const juce::String origin = shape.factory ? juce::String() : juce::String("yours");
    row.detail = !inMixedList ? origin : kindName(shape.kind) + (origin.isEmpty() ? juce::String() : ", " + origin);
    row.shape = shape;
    return row;
}

void SoundBrowserComponent::appendListedRows(const juce::StringArray& ids,
                                             const synth::soundbrowser::SoundFilter& filter) {
    for (const auto& id : ids) {
        if (const auto ref = shapeFromPayload(id)) {
            if (filter.isActive())
                continue;
            if (const auto* shape = cachedShape(ref->kind, ref->factory, ref->name))
                rows_.push_back(makeShapeRow(*shape, true));
        } else if (const auto entry = index_.find(id);
                   entry && synth::soundbrowser::soundPassesFilter(*entry, filter)) {
            appendSoundRows({*entry});
        }
    }
}

// Two groups, each led by a heading; the library lists a kind's saved shapes before its built-in ones.
void SoundBrowserComponent::rebuildShapeRows() {
    for (const auto* group : {&filterShapes_, &lfoShapes_}) {
        if (group->empty())
            continue;
        rows_.push_back({RowKind::Hint, groupTitle(group->front().kind), {}, {}, {}, 0, std::nullopt});
        for (const auto& shape : *group)
            rows_.push_back(makeShapeRow(shape, false));
    }
    if (rows_.empty())
        rows_.push_back({RowKind::Hint, "No shapes yet", {}, {}, {}, 0, std::nullopt});
}

juce::String SoundBrowserComponent::shapeTooltip(const Row& row) const {
    const bool filter = row.shape->kind == synth::shapes::ShapeKind::Filter;
    const juce::String target = filter ? "Filter" : "LFO";
    auto text = row.text + ", " + shapeKindAndSource(*row.shape) + ". Drag it onto a " + target +
                "'s shape chip or Custom card. Or select a " + target +
                " set to Custom and press Return to stamp it; "
                "Return needs that card selected. F adds it to Favourites.";
    if (!row.shape->factory)
        text += " Right-click, or press Shift+F10, for Rename, Delete and Show in " + fileManagerName() + ".";
    return text;
}

juce::String SoundBrowserComponent::getShapeFooterText() const {
    if (selected_ < 0 || selected_ >= (int)rows_.size() || rows_[(size_t)selected_].kind != RowKind::Shape)
        return {};
    return shapeKindAndSource(*rows_[(size_t)selected_].shape);
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
    const auto line = makeShapeCurvePath(row.shape->curve, box.reduced(3.0f, 3.0f), kThumbWidth - 6);
    g.setColour(c.accent);
    g.strokePath(line, juce::PathStrokeType(kThumbLineWidth));

    const juce::Font detailFont(juce::FontOptions(12.0f));
    const int detailWidth =
        row.detail.isEmpty() ? 0 : (int)std::ceil(juce::GlyphArrangement::getStringWidth(detailFont, row.detail));
    const int nameLeft = area.getX() + side + kThumbWidth + kThumbGap;
    int nameRight = area.getRight() - side - (detailWidth > 0 ? detailWidth + 8 : 0);
    if (lists_.isFavourite(synth::shapes::shapeId(row.shape->kind, row.shape->factory, row.shape->name))) {
        juce::Path star;
        star.addStar({(float)nameRight - 7.0f, (float)area.getCentreY()}, 5, 2.6f, 5.4f);
        g.setColour(c.accent);
        g.fillPath(star);
        nameRight -= 18;
    }
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
