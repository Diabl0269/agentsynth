// ShapeMapController.cpp -- map gestures as undoable shape-list edits.
#include "ShapeMapController.h"
#include <cmath>

namespace synth::ui {

namespace {
constexpr float kDuplicateOffset = 0.07f;
constexpr int kNameLimit = 64;

float clamp01(float v) { return juce::jlimit(0.0f, 1.0f, v); }

void setPosition(ShapeItem& shape, float x, float y) {
    shape.position.resize(2, 0.5f);
    shape.position[0] = clamp01(x);
    shape.position[1] = clamp01(y);
}

float positionOf(const ShapeItem& shape, int axis) {
    return (int)shape.position.size() > axis ? shape.position[(size_t)axis] : 0.5f;
}
} // namespace

/** Dot drags are one bracket each (begin on the first movement, end on release), the cursor is the adapter's own
    gesture, and every discrete action below brackets itself. */
ShapeMapController::ShapeMapController(MorphMap& map, ShapeListAdapter& adapter)
    : map_(map)
    , adapter_(adapter)
    , shapesWatch_(adapter.restoreSerial())
    , cursorWatch_(adapter.restoreSerial()) {
    map_.onGestureStart = [this](MorphMap::Gesture g) {
        if (g == MorphMap::Gesture::Cursor)
            adapter_.beginCursorGesture();
        else
            adapter_.beginEdit();
    };
    map_.onGestureEnd = [this](MorphMap::Gesture g) {
        if (g == MorphMap::Gesture::Cursor)
            adapter_.endCursorGesture();
        else
            adapter_.endEdit();
    };
    map_.onCursorMoved = [this](float x, float y) { adapter_.setCursor({x, y}); };
    map_.onDotMoved = [this](int index, float x, float y) {
        auto shapes = adapter_.getShapes();
        if (index < 0 || index >= (int)shapes.size())
            return;
        setPosition(shapes[(size_t)index], x, y);
        adapter_.setShapes(shapes);
        if (onChanged)
            onChanged();
    };
    map_.onAddAt = [this](float x, float y) { addShapeAt(x, y); };
    map_.onSelect = [this](int index) {
        if (onSelect)
            onSelect(index);
    };
    map_.onEditDot = [this](int index) {
        if (onEdit)
            onEdit(index);
    };
    map_.onDuplicateDot = [this](int index) { duplicateShape(index); };
    map_.onDeleteDot = [this](int index) { deleteShape(index); };
    map_.onRenameDot = [this](int index, const juce::String& name) { renameShape(index, name); };
    map_.canDeleteDot = [this] { return (int)adapter_.getShapes().size() > 1; };
}

/** True while a restore runs or one finished since `seenSerial` was taken; takes the serial. */
bool ShapeMapController::restoreSince(RestoreWatch& watch) const {
    return watch.take(adapter_.isRestoring(), adapter_.restoreSerial());
}

void ShapeMapController::refresh() {
    std::vector<MorphMap::Dot> dots;
    for (const auto& shape : adapter_.getShapes())
        dots.push_back({shape.name, positionOf(shape, 0), positionOf(shape, 1)});
    map_.setDots(std::move(dots), restoreSince(shapesWatch_));
    map_.setCursorVisible(adapter_.hasCursor());
    syncCursor();
}

void ShapeMapController::syncCursor() {
    if (!adapter_.hasCursor())
        return;
    const auto cursor = adapter_.getCursor();
    const bool restored = restoreSince(cursorWatch_);
    const auto shown = map_.getCursor();
    if (std::abs(shown.x - cursor.x) > 1.0e-4f || std::abs(shown.y - cursor.y) > 1.0e-4f)
        map_.setCursor(cursor.x, cursor.y, restored);
}

void ShapeMapController::changed() {
    refresh();
    if (onChanged)
        onChanged();
}

/** "Shape N", counting up past any name already taken, so two shapes never share a name (a screen reader would
    read them out identically). */
juce::String ShapeMapController::uniqueName(const std::vector<ShapeItem>& shapes, const juce::String& wanted) const {
    auto taken = [&](const juce::String& name) {
        for (const auto& s : shapes)
            if (s.name == name)
                return true;
        return false;
    };
    const juce::String base = wanted.substring(0, kNameLimit - 4);
    juce::String name = base;
    for (int n = 2; taken(name); ++n)
        name = base + " " + juce::String(n);
    return name;
}

int ShapeMapController::addShapeAt(float x, float y) {
    auto shapes = adapter_.getShapes();
    if ((int)shapes.size() >= adapter_.getMaxShapes())
        return -1;
    adapter_.beginEdit();
    ShapeItem shape = adapter_.makeShapeAt(clamp01(x), clamp01(y));
    shape.name =
        uniqueName(shapes, shape.name.isEmpty() ? "Shape " + juce::String((int)shapes.size() + 1) : shape.name);
    setPosition(shape, x, y);
    shapes.push_back(std::move(shape));
    adapter_.setShapes(shapes);
    adapter_.endEdit();
    changed();
    const int index = (int)shapes.size() - 1;
    map_.setSelected(index);
    if (onSelect)
        onSelect(index);
    return index;
}

/** The candidate on a 5 x 5 grid furthest from every existing dot, so the Add button never stacks a shape on one. */
int ShapeMapController::addShapeInFreeSpot() {
    const auto shapes = adapter_.getShapes();
    float bestX = 0.5f, bestY = 0.5f, bestDistance = -1.0f;
    for (int gy = 0; gy < 5; ++gy)
        for (int gx = 0; gx < 5; ++gx) {
            const float x = 0.1f + 0.2f * (float)gx;
            const float y = 0.1f + 0.2f * (float)gy;
            float nearest = 10.0f;
            for (const auto& s : shapes)
                nearest = juce::jmin(nearest, std::hypot(x - positionOf(s, 0), y - positionOf(s, 1)));
            if (nearest > bestDistance) {
                bestDistance = nearest;
                bestX = x;
                bestY = y;
            }
        }
    return addShapeAt(bestX, bestY);
}

int ShapeMapController::duplicateShape(int index) {
    auto shapes = adapter_.getShapes();
    if (index < 0 || index >= (int)shapes.size() || (int)shapes.size() >= adapter_.getMaxShapes())
        return -1;
    adapter_.beginEdit();
    ShapeItem copy = shapes[(size_t)index];
    copy.name = uniqueName(shapes, copy.name + " copy");
    setPosition(copy, positionOf(copy, 0) + kDuplicateOffset, positionOf(copy, 1) + kDuplicateOffset);
    shapes.insert(shapes.begin() + index + 1, std::move(copy));
    adapter_.setShapes(shapes);
    adapter_.endEdit();
    changed();
    map_.setSelected(index + 1);
    if (onSelect)
        onSelect(index + 1);
    return index + 1;
}

/** At least one shape always stays. */
bool ShapeMapController::deleteShape(int index) {
    auto shapes = adapter_.getShapes();
    if (shapes.size() <= 1 || index < 0 || index >= (int)shapes.size())
        return false;
    adapter_.beginEdit();
    shapes.erase(shapes.begin() + index);
    adapter_.setShapes(shapes);
    adapter_.endEdit();
    changed();
    const int next = juce::jmin(index, (int)shapes.size() - 1);
    map_.setSelected(next);
    if (onSelect)
        onSelect(next);
    return true;
}

void ShapeMapController::renameShape(int index, const juce::String& name) {
    auto shapes = adapter_.getShapes();
    if (index < 0 || index >= (int)shapes.size() || name.trim().isEmpty())
        return;
    adapter_.beginEdit();
    shapes[(size_t)index].name = name.trim().substring(0, kNameLimit);
    adapter_.setShapes(shapes);
    adapter_.endEdit();
    changed();
}

} // namespace synth::ui
