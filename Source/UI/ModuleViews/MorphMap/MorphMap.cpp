// MorphMap.cpp -- the morph map's content, geometry, pointer handling and dot motion. Painting is MorphMapPaint.cpp;
// the keyboard stops, menu, rename and screen-reader names are MorphMapKeyboard.cpp.
#include "MorphMap.h"
#include "MorphMapInternal.h"
#include "UI/Layout/ControlMotion.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/Layout/ReorderDrag/ReorderFramePump.h"
#include <algorithm>
#include <cmath>

namespace synth::ui {

namespace {
constexpr float kHitRadius = 10.0f;
constexpr float kStripGap = 6.0f;
constexpr float kBarGap = 2.0f;
constexpr float kBarHitPad = 3.0f;
constexpr float kDotGap = 2.0f;
constexpr float kEdgeMargin = 1.0f;
// A margin above the plot; the sides and the bottom take stripReach().
constexpr float kTopInset = 10.0f;
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
    // An undo or redo glide moves the drawn dots: their keyboard stops (and focus rings) ride with them every frame.
    moveTween_.onDirty = [this](juce::Rectangle<int>) {
        layoutStops();
        repaint();
    };
}

MorphMap::~MorphMap() {
    if (glidePump_ != nullptr)
        glidePump_->stop();
}

/** From the plot's edge to the far side of a cable's landing dot: the slider with its gap, the modulation bar, and
    the dot with its own gap, so the dot never covers the bar or the slider and never leaves the map. */
float MorphMap::stripReach() const {
    return kStripGap + (float)kStripThickness + kBarGap + kModBarThickness + kDotGap + landingDot_ + kEdgeMargin;
}

/** The plot is inset so a dot or the cursor at 0 or 1 is not clipped and the sliders sit outside it. The left and
    right insets are equal (the right edge is kept free for a third axis, so a right-hand slider can join without
    moving the plot) and the bottom one holds the bottom slider. */
juce::Rectangle<float> MorphMap::getPlotArea() const {
    const float reach = stripReach();
    return getLocalBounds()
        .toFloat()
        .withTrimmedLeft(reach)
        .withTrimmedRight(reach)
        .withTrimmedTop(kTopInset)
        .withTrimmedBottom(reach);
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

void MorphMap::setDots(std::vector<Dot> dots, bool glideMoved) {
    auto before = std::move(dots_);
    dots_ = std::move(dots);
    startMoveGlide(before, glideMoved);
    startDotMotion(before);
    selected_ = dots_.empty() ? -1 : juce::jmin(selected_, (int)dots_.size() - 1);
    if (renameEditor_ != nullptr)
        endRename(false);
    rebuildStops();
    repaint();
}

void MorphMap::setCursor(float x, float y, bool glide) {
    const auto before = getDrawnCursor();
    cursor_ = {juce::jlimit(kMapMin, kMapMax, x), juce::jlimit(kMapMin, kMapMax, y)};
    moveTween_.forceAnimateForTest(animatesOffScreenForTest());
    if (glide)
        moveTween_.retarget(kCursorKey, before, cursor_, true);
    else
        moveTween_.erase(kCursorKey);
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

// Each stop sits where its dot or the cursor is drawn, so the focus ring is on the dot while an undo glide runs.
void MorphMap::layoutStops() {
    for (int i = 0; i < (int)dotStops_.size(); ++i)
        dotStops_[(size_t)i]->setCentrePosition(toPixels(getDrawnDot(i).x, getDrawnDot(i).y).roundToInt());
    cursorStop_->setCentrePosition(toPixels(getDrawnCursor().x, getDrawnCursor().y).roundToInt());
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

/** Past the modulation bar by the dot's radius and a gap, the same push-out a knob's or fader's landing point has. */
juce::Point<float> MorphMap::stripAnchor(Axis axis) const {
    const auto strip = getStripBounds(axis);
    const auto track = getModBarTrack(axis);
    const float push = kDotGap + landingDot_ * 0.5f;
    if (axis == Axis::X)
        return {strip.getCentreX(), track.getBottom() + push};
    if (axis == Axis::Y)
        return {track.getX() - push, strip.getCentreY()};
    return getLocalBounds().toFloat().getCentre();
}

void MorphMap::setLandingDotDiameter(float diameter) {
    if (diameter == landingDot_)
        return;
    landingDot_ = diameter;
    layoutStops();
    repaint();
}

juce::Rectangle<float> MorphMap::getModBarTrack(Axis axis) const {
    const auto strip = getStripBounds(axis);
    if (axis == Axis::X)
        return {strip.getX(), strip.getBottom() + kBarGap, strip.getWidth(), kModBarThickness};
    if (axis == Axis::Y)
        return {strip.getX() - kBarGap - kModBarThickness, strip.getY(), kModBarThickness, strip.getHeight()};
    return {};
}

juce::Rectangle<float> MorphMap::getModBarBetween(Axis axis, float fromNorm, float toNorm) const {
    const auto track = getModBarTrack(axis);
    if (axis == Axis::X) {
        const float a = track.getX() + track.getWidth() * fromNorm;
        const float b = track.getX() + track.getWidth() * toNorm;
        return {std::min(a, b), track.getY(), std::abs(a - b), track.getHeight()};
    }
    if (axis == Axis::Y) {
        const float a = track.getY() + track.getHeight() * fromNorm;
        const float b = track.getY() + track.getHeight() * toNorm;
        return {track.getX(), std::min(a, b), track.getWidth(), std::abs(a - b)};
    }
    return {};
}

MorphMap::Axis MorphMap::modBarAt(juce::Point<float> point) const {
    if (!cursorVisible_)
        return Axis::None;
    for (const Axis axis : {Axis::X, Axis::Y})
        if (getModBarTrack(axis).expanded(kBarHitPad).contains(point))
            return axis;
    return Axis::None;
}

void MorphMap::setStripModulation(Axis axis, std::vector<StripModulation> modulation) {
    if (axis == Axis::None)
        return;
    auto& held = stripModulation_[axis == Axis::X ? 0 : 1];
    if (held == modulation)
        return;
    held = std::move(modulation);
    repaint();
}

const std::vector<MorphMap::StripModulation>& MorphMap::getStripModulation(Axis axis) const {
    static const std::vector<StripModulation> none;
    return axis == Axis::None ? none : stripModulation_[axis == Axis::X ? 0 : 1];
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
    pressClaimed_ = pressGesture && pressGesture(e, 0);
    if (pressClaimed_) {
        drag_ = Drag::None;
        return;
    }
    drag_ = Drag::None;
    dragMoved_ = false;
    gestureOpen_ = false;
    const int dot = dotAt(e.position);
    const bool onCursor = cursorAt(e.position);

    if (e.mods.isPopupMenu()) {
        if (dot >= 0 && !onCursor) {
            select(dot);
            showDotMenu(dot);
        } else if (const Axis strip = stripAt(e.position); strip != Axis::None && cursorVisible_) {
            showStripMenu(strip);
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
        grabOffset_ = getCursorCentre() - e.position;
        takeFocus(*cursorStop_);
    } else if (dot >= 0) {
        drag_ = Drag::Dot;
        dragIndex_ = dot;
        grabOffset_ = getDotCentre(dot) - e.position;
        takeFocus(*dotStops_[(size_t)dot]);
        select(dot);
    } else {
        drag_ = Drag::Empty;
        takeFocus(*this);
    }
}

void MorphMap::mouseDrag(const juce::MouseEvent& e) {
    if (pressClaimed_) {
        pressGesture(e, 1);
        return;
    }
    if (drag_ == Drag::Strip) {
        moveToStrip(dragAxis_, e.position);
        return;
    }
    if (drag_ != Drag::Dot && drag_ != Drag::Cursor)
        return;
    const auto to = toMap(e.position + grabOffset_);
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
    if (pressClaimed_) {
        pressClaimed_ = false;
        pressGesture(e, 2);
        return;
    }
    const Drag was = drag_;
    drag_ = Drag::None;
    if (gestureOpen_) {
        gestureOpen_ = false;
        if (onGestureEnd)
            onGestureEnd(was == Drag::Dot ? Gesture::Dot : Gesture::Cursor);
        return;
    }
}

/** Double-click on a dot deletes it (one always stays); on empty space it adds a dot there and focuses it so Delete
    or Backspace removes it at once. A single click does neither. */
void MorphMap::mouseDoubleClick(const juce::MouseEvent& e) {
    if (e.mods.isPopupMenu() || pressClaimed_ || stripAt(e.position) != Axis::None || cursorAt(e.position))
        return;
    if (const int dot = dotAt(e.position); dot >= 0) {
        if (!canDeleteDot || canDeleteDot())
            deleteDotAndRefocus(dot);
        return;
    }
    if (!onAddAt)
        return;
    const int before = (int)dots_.size();
    const auto at = toMap(e.position);
    onAddAt(at.x, at.y);
    if ((int)dots_.size() > before && selected_ >= 0 && selected_ < (int)dotStops_.size())
        takeFocus(*dotStops_[(size_t)selected_]);
}

void MorphMap::mouseMove(const juce::MouseEvent& e) {
    const bool overThing = cursorAt(e.position) || dotAt(e.position) >= 0;
    setMouseCursor(overThing ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
    const Axis bar = modBarAt(e.position);
    setHoveredAxis(bar != Axis::None || !cursorVisible_ ? bar : stripAt(e.position));
}

void MorphMap::mouseExit(const juce::MouseEvent&) { setHoveredAxis(Axis::None); }

void MorphMap::setHoveredAxis(Axis axis) {
    if (axis == hoverAxis_)
        return;
    hoverAxis_ = axis;
    if (onStripHover)
        onStripHover(axis);
}

void MorphMap::moveDot(int index, juce::Point<float> to) {
    if (index < 0 || index >= (int)dots_.size())
        return;
    moveTween_.erase(index);
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
    moveTween_.erase(kCursorKey);
    cursor_ = to;
    layoutStops();
    refreshStopNames();
    repaint();
    if (onCursorMoved)
        onCursorMoved(to.x, to.y);
}

// ---------------------------------------------------------------------------------------------------------------
// Dot motion: a dot that is added (or restored by an undo) grows in with the 8% bounce and a removed one shrinks out
// where it was (docs/layout/animation.md, "Motion rules"). Dots are keyed by name, which the shape list keeps unique,
// so an undone delete is the same dot coming back. One AnimationDriver runs all of it for a finite time.
// ---------------------------------------------------------------------------------------------------------------

namespace {
constexpr double kGrowMs = control_motion::kGrowMs;
constexpr double kShrinkOutMs = 180.0;
constexpr double kRunMs = kGrowMs;

bool& animateOffScreen() {
    static bool value = false;
    return value;
}
} // namespace

void MorphMap::setAnimateOffScreenForTest(bool animate) { animateOffScreen() = animate; }

bool MorphMap::animatesOffScreenForTest() { return animateOffScreen(); }

const MorphMap::DotMotion* MorphMap::growingMotion(int index) const {
    if (index < 0 || index >= (int)dots_.size())
        return nullptr;
    for (const auto& m : motions_)
        if (!m.leaving && m.id == dots_[(size_t)index].name)
            return &m;
    return nullptr;
}

float MorphMap::getDotScale(int index) const {
    const auto* m = growingMotion(index);
    return m == nullptr || motionReduced_ ? 1.0f : control_motion::growScale(m->progress);
}

float MorphMap::getDotAlpha(int index) const {
    const auto* m = growingMotion(index);
    return m == nullptr || !motionReduced_ ? 1.0f : juce::jlimit(0.0f, 1.0f, easeOutCubic(m->progress));
}

int MorphMap::getNumLeavingDots() const noexcept {
    return (int)std::count_if(motions_.begin(), motions_.end(), [](const auto& m) { return m.leaving; });
}

bool MorphMap::isMoving() const noexcept { return !motions_.empty(); }

/** Called with the dots as they were just before `setDots`. Exactly one dot more or fewer is a motion; any other
    change (a rename, a move, a whole new list, the first fill) lands at once. A running motion carries on from where
    each dot is (an interrupted shrink does not restart). */
void MorphMap::startDotMotion(const std::vector<Dot>& before) {
    if (animationsOff() || (!isShowing() && !animateOffScreen()))
        return endMotion();
    const bool added = dots_.size() == before.size() + 1;
    const bool removed = before.size() == dots_.size() + 1;
    if (before.empty() || (!added && !removed))
        return;
    const auto& larger = added ? dots_ : before;
    const auto& smaller = added ? before : dots_;
    size_t at = 0;
    while (at < smaller.size() && larger[at].name == smaller[at].name)
        ++at;
    for (size_t i = at; i < smaller.size(); ++i)
        if (larger[i + 1].name != smaller[i].name)
            return;

    motionReduced_ = prefersReducedMotion();
    const auto& dot = larger[at];
    std::erase_if(motions_, [&](const auto& m) { return m.id == dot.name; });
    DotMotion motion;
    motion.id = dot.name;
    motion.leaving = removed;
    motion.dot = dot;
    motion.colourIndex = (int)at;
    motions_.push_back(std::move(motion));

    motionLastMs_ = 0.0;
    motionDriver_.start(
        motionUpdater_, kRunMs, [](float t) { return t; }, [this](float t) { motionFrame((double)t * kRunMs); },
        [this] { endMotion(); });
}

void MorphMap::motionFrame(double elapsedMs) {
    const double dt = elapsedMs - motionLastMs_;
    motionLastMs_ = elapsedMs;
    for (auto& m : motions_) {
        const double duration = motionReduced_ ? control_motion::kReducedMs : (m.leaving ? kShrinkOutMs : kGrowMs);
        m.progress = juce::jmin(1.0f, m.progress + (float)(dt / duration));
    }
    std::erase_if(motions_, [](const auto& m) { return m.progress >= 1.0f; });
    repaint();
}

void MorphMap::endMotion() {
    motionDriver_.stop(motionUpdater_);
    motions_.clear();
    repaint();
}

void MorphMap::stepMotionForTest(double ms) { motionDriver_.stepForTest(ms); }

} // namespace synth::ui
