// MorphMap.cpp -- the morph map's content, geometry and pointer handling. Painting is MorphMapPaint.cpp; the
// keyboard stops, menu, rename and screen-reader names are MorphMapKeyboard.cpp.
#include "MorphMap.h"
#include "MorphMapInternal.h"
#include "UI/Layout/ReorderDrag/ReorderFramePump.h"
#include <cmath>

namespace synth::ui {

namespace {
constexpr float kHitRadius = 10.0f;
constexpr float kStripGap = 6.0f;
// Room each side of the plot: the left slider, the right edge kept free for a third axis, a margin above, and the
// bottom slider with its gap. The map is symmetric left and right so a right-hand slider can join without moving it.
constexpr float kSideInset = 22.0f;
constexpr float kTopInset = 10.0f;
constexpr float kBottomInset = 22.0f;
constexpr float kMapMin = 0.0f;
constexpr float kMapMax = 1.0f;
} // namespace

MorphMap::MorphMap() {
    setTitle("Morph map");
    setDescription("Shapes placed on a map; the cursor blends the shapes around it");
    setWantsKeyboardFocus(true);
    glideHost_ = makeDefaultGlideHost();
    cursorStop_ = std::make_unique<Stop>(*this, -1);
    addAndMakeVisible(*cursorStop_);
    cursorStop_->setExplicitFocusOrder(1000);
    for (int a = 0; a < 2; ++a) {
        stripStops_[a] = std::make_unique<Stop>(*this, -2 - a);
        stripStops_[a]->setExplicitFocusOrder(1001 + a);
        addAndMakeVisible(*stripStops_[a]);
    }
    refreshStopNames();
}

MorphMap::~MorphMap() {
    if (glidePump_ != nullptr)
        glidePump_->stop();
}

/** The plot is inset so a dot or the cursor at 0 or 1 is not clipped and the sliders sit outside it. */
juce::Rectangle<float> MorphMap::getPlotArea() const {
    return getLocalBounds()
        .toFloat()
        .withTrimmedLeft(kSideInset)
        .withTrimmedRight(kSideInset)
        .withTrimmedTop(kTopInset)
        .withTrimmedBottom(kBottomInset);
}

juce::Point<float> MorphMap::toPixels(float x, float y) const {
    const auto plot = getPlotArea();
    return {plot.getX() + x * plot.getWidth(), plot.getY() + y * plot.getHeight()};
}

juce::Point<float> MorphMap::toMap(juce::Point<float> pixels) const {
    const auto plot = getPlotArea();
    if (plot.getWidth() <= 0.0f || plot.getHeight() <= 0.0f)
        return {0.5f, 0.5f};
    return {juce::jlimit(kMapMin, kMapMax, (pixels.x - plot.getX()) / plot.getWidth()),
            juce::jlimit(kMapMin, kMapMax, (pixels.y - plot.getY()) / plot.getHeight())};
}

juce::Point<float> MorphMap::getDotCentre(int index) const {
    if (index < 0 || index >= (int)dots_.size())
        return {};
    return toPixels(dots_[(size_t)index].x, dots_[(size_t)index].y);
}

juce::Point<float> MorphMap::getCursorCentre() const { return toPixels(cursor_.x, cursor_.y); }

/** The nearest dot within kHitRadius; on a tie the later dot wins because it is painted on top. */
int MorphMap::dotAt(juce::Point<float> point) const {
    int best = -1;
    float bestDistance = kHitRadius;
    for (int i = 0; i < (int)dots_.size(); ++i) {
        const float d = getDotCentre(i).getDistanceFrom(point);
        if (d <= bestDistance) {
            best = i;
            bestDistance = d;
        }
    }
    return best;
}

bool MorphMap::cursorAt(juce::Point<float> point) const {
    return cursorVisible_ && getCursorCentre().getDistanceFrom(point) <= kHitRadius;
}

// ---------------------------------------------------------------------------------------------------------------
// Content
// ---------------------------------------------------------------------------------------------------------------

void MorphMap::setDots(std::vector<Dot> dots) {
    dots_ = std::move(dots);
    selected_ = dots_.empty() ? -1 : juce::jmin(selected_, (int)dots_.size() - 1);
    if (renameEditor_ != nullptr)
        endRename(false);
    rebuildStops();
    repaint();
}

void MorphMap::setCursor(float x, float y) {
    cursor_ = {juce::jlimit(kMapMin, kMapMax, x), juce::jlimit(kMapMin, kMapMax, y)};
    layoutStops();
    refreshStopNames();
    repaint();
}

void MorphMap::setCursorVisible(bool visible) {
    cursorVisible_ = visible;
    cursorStop_->setVisible(visible);
    for (auto& stop : stripStops_)
        stop->setVisible(visible);
    repaint();
}

void MorphMap::setSelected(int index) {
    index = juce::jlimit(-1, (int)dots_.size() - 1, index);
    if (index == selected_)
        return;
    selected_ = index;
    repaint();
}

void MorphMap::setShowLabels(bool show) {
    showLabels_ = show;
    repaint();
}

/** One stop per dot, in dot order (their explicit focus orders put them before the cursor's). The stops that are
    already there are kept: a key step refreshes the map, and replacing the focused stop would drop keyboard focus
    after every step. Only the missing ones are created and the surplus ones removed from the end; if the focused
    stop is one of those, focus moves to the nearest dot that remains (or the map when none does). */
void MorphMap::rebuildStops() {
    int focusedIndex = -1;
    for (int i = 0; i < (int)dotStops_.size(); ++i)
        if (dotStops_[(size_t)i]->hasKeyboardFocus(false))
            focusedIndex = i;
    const int wanted = (int)dots_.size();
    if (glide_.isHeld() && glideStopIndex_ >= wanted)
        endGlide();
    while ((int)dotStops_.size() > wanted)
        dotStops_.pop_back();
    for (int i = (int)dotStops_.size(); i < wanted; ++i) {
        auto stop = std::make_unique<Stop>(*this, i);
        stop->setExplicitFocusOrder(i + 1);
        addAndMakeVisible(*stop);
        dotStops_.push_back(std::move(stop));
    }
    layoutStops();
    refreshStopNames();
    if (focusedIndex >= wanted)
        takeFocus(wanted > 0 ? *dotStops_.back() : static_cast<juce::Component&>(*this));
}

void MorphMap::layoutStops() {
    for (int i = 0; i < (int)dotStops_.size(); ++i)
        dotStops_[(size_t)i]->setCentrePosition(getDotCentre(i).roundToInt());
    cursorStop_->setCentrePosition(getCursorCentre().roundToInt());
    stripStops_[0]->setBounds(getStripBounds(Axis::X).expanded(2.0f).toNearestInt());
    stripStops_[1]->setBounds(getStripBounds(Axis::Y).expanded(2.0f).toNearestInt());
}

void MorphMap::resized() {
    layoutStops();
    repaint();
}

// ---------------------------------------------------------------------------------------------------------------
// Cables
// ---------------------------------------------------------------------------------------------------------------

juce::Rectangle<float> MorphMap::getStripBounds(Axis axis) const {
    const auto plot = getPlotArea();
    constexpr float kT = (float)kStripThickness;
    if (axis == Axis::X)
        return {plot.getX(), plot.getBottom() + kStripGap, plot.getWidth(), kT};
    if (axis == Axis::Y)
        return {plot.getX() - kStripGap - kT, plot.getY(), kT, plot.getHeight()};
    return {};
}

MorphMap::Axis MorphMap::stripAt(juce::Point<float> point) const {
    if (getStripBounds(Axis::X).expanded(2.0f).contains(point))
        return Axis::X;
    if (getStripBounds(Axis::Y).expanded(2.0f).contains(point))
        return Axis::Y;
    return Axis::None;
}

juce::Point<float> MorphMap::stripAnchor(Axis axis) const {
    const auto strip = getStripBounds(axis);
    if (axis == Axis::X)
        return {strip.getCentreX(), strip.getBottom() + 3.0f};
    if (axis == Axis::Y)
        return {strip.getX() - 3.0f, strip.getCentreY()};
    return getLocalBounds().toFloat().getCentre();
}

bool MorphMap::setDropHighlight(Axis axis) {
    if (dropAxis_ == axis)
        return false;
    dropAxis_ = axis;
    repaint();
    return true;
}

// ---------------------------------------------------------------------------------------------------------------
// Pointer
// ---------------------------------------------------------------------------------------------------------------

void MorphMap::select(int index) {
    if (index == selected_)
        return;
    selected_ = index;
    repaint();
    if (onSelect && index >= 0)
        onSelect(index);
}

/** Focus can only move to a component that is on screen. */
void MorphMap::takeFocus(juce::Component& target) {
    if (isShowing())
        target.grabKeyboardFocus();
}

/** A press picks what it is on (the cursor wins over a dot beneath it) and takes keyboard focus there; the
    gesture bracket opens on the first real movement, so a plain click never reaches the undo stack. */
void MorphMap::mouseDown(const juce::MouseEvent& e) {
    if (renameEditor_ != nullptr)
        endRename(true);
    drag_ = Drag::None;
    dragMoved_ = false;
    gestureOpen_ = false;
    const int dot = dotAt(e.position);
    const bool onCursor = cursorAt(e.position);

    if (e.mods.isPopupMenu()) {
        if (dot >= 0 && !onCursor) {
            select(dot);
            showDotMenu(dot);
        }
        return;
    }
    if (const Axis strip = stripAt(e.position); strip != Axis::None && cursorVisible_) {
        drag_ = Drag::Strip;
        dragAxis_ = strip;
        takeFocus(*stripStops_[strip == Axis::X ? 0 : 1]);
        gestureOpen_ = true;
        dragMoved_ = true;
        if (onGestureStart)
            onGestureStart(Gesture::Cursor);
        moveToStrip(strip, e.position);
    } else if (onCursor) {
        drag_ = Drag::Cursor;
        takeFocus(*cursorStop_);
    } else if (dot >= 0) {
        drag_ = Drag::Dot;
        dragIndex_ = dot;
        takeFocus(*dotStops_[(size_t)dot]);
        select(dot);
    } else {
        drag_ = Drag::Empty;
        takeFocus(*this);
    }
}

void MorphMap::mouseDrag(const juce::MouseEvent& e) {
    if (drag_ == Drag::Strip) {
        moveToStrip(dragAxis_, e.position);
        return;
    }
    if (drag_ != Drag::Dot && drag_ != Drag::Cursor)
        return;
    const auto to = toMap(e.position);
    if (!dragMoved_) {
        dragMoved_ = true;
        gestureOpen_ = true;
        if (onGestureStart)
            onGestureStart(drag_ == Drag::Dot ? Gesture::Dot : Gesture::Cursor);
    }
    if (drag_ == Drag::Cursor)
        moveCursor(to);
    else
        moveDot(dragIndex_, to);
}

void MorphMap::mouseUp(const juce::MouseEvent& e) {
    const Drag was = drag_;
    drag_ = Drag::None;
    if (gestureOpen_) {
        gestureOpen_ = false;
        if (onGestureEnd)
            onGestureEnd(was == Drag::Dot ? Gesture::Dot : Gesture::Cursor);
        return;
    }
    if (was == Drag::Empty && !dragMoved_ && !e.mods.isPopupMenu() && onAddAt) {
        const auto at = toMap(e.position);
        onAddAt(at.x, at.y);
    }
}

void MorphMap::mouseDoubleClick(const juce::MouseEvent& e) {
    const int dot = cursorAt(e.position) ? -1 : dotAt(e.position);
    if (dot >= 0 && onEditDot)
        onEditDot(dot);
}

void MorphMap::mouseMove(const juce::MouseEvent& e) {
    const bool overThing = cursorAt(e.position) || dotAt(e.position) >= 0;
    setMouseCursor(overThing ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
}

void MorphMap::moveDot(int index, juce::Point<float> to) {
    if (index < 0 || index >= (int)dots_.size())
        return;
    dots_[(size_t)index].x = to.x;
    dots_[(size_t)index].y = to.y;
    layoutStops();
    refreshStopNames();
    repaint();
    if (onDotMoved)
        onDotMoved(index, to.x, to.y);
}

/** A press or drag on a slider sets that one coordinate of the cursor from the pointer along the strip. */
void MorphMap::moveToStrip(Axis axis, juce::Point<float> pixels) {
    const auto mapped = toMap(pixels);
    moveCursor(axis == Axis::X ? juce::Point<float>{mapped.x, cursor_.y} : juce::Point<float>{cursor_.x, mapped.y});
}

void MorphMap::moveCursor(juce::Point<float> to) {
    cursor_ = to;
    layoutStops();
    refreshStopNames();
    repaint();
    if (onCursorMoved)
        onCursorMoved(to.x, to.y);
}

} // namespace synth::ui
