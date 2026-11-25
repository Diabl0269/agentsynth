#include "UI/Graph/CanvasEdgeScroll/CanvasEdgeScrollDriver.h"

#include "UI/Graph/CanvasFrame/CanvasFrame.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Layout/PointerState.h"
#include "UI/Timeline/EdgeAutoScroll.h"

#include <algorithm>

namespace synth::ui {

CanvasEdgeScrollDriver::CanvasEdgeScrollDriver(GraphEditor& editor)
    : editor_(editor)
    , marker_(std::make_unique<CanvasEdgeLimitMarker>(editor))
    , scroller_(editor, CanvasEdgeScroller::Hooks{[this] { return editor_.getLocalBounds(); },
                                                  [this] { return pointerInView(); },
                                                  [this](juce::Point<float> d) { return panBy(d); },
                                                  [this] {
                                                      // The mouse source's own button state as well as the hardware's:
                                                      // test mode injects its presses into the window's peer, which the
                                                      // hardware never sees.
                                                      return testPointer_ != nullptr ||
                                                             synth::ui::isPointerDragging() ||
                                                             synth::ui::currentModifiers().isAnyMouseButtonDown();
                                                  },
                                                  [this](juce::Point<float> target) { aim(target); }}) {
    editor_.addChildComponent(*marker_);
    juce::Desktop::getInstance().addGlobalMouseListener(this);
}

CanvasEdgeScrollDriver::~CanvasEdgeScrollDriver() { juce::Desktop::getInstance().removeGlobalMouseListener(this); }

void CanvasEdgeScrollDriver::notePointer(const juce::MouseEvent& e) {
    if (!inSyntheticDrag_)
        lastScreenPosition_ = e.getScreenPosition().toFloat();
}

// Any component on the canvas, not only the canvas itself: a press on empty space can land on a child laid over it
// (a hull, a grid layer) that hands the gesture to GraphEditor, and that child is the event's component.
void CanvasEdgeScrollDriver::noteIfOnCanvas(const juce::MouseEvent& e) {
    if (e.eventComponent == &editor_ || editor_.isParentOf(e.eventComponent))
        notePointer(e);
}

juce::Point<int> CanvasEdgeScrollDriver::keepInView(juce::Point<int> p) const {
    return {keepInsideSpan(p.x, 0, editor_.getWidth()), keepInsideSpan(p.y, 0, editor_.getHeight())};
}

juce::Point<int> CanvasEdgeScrollDriver::keepScreenInView(juce::Point<int> screen) const {
    return editor_.localPointToGlobal(keepInView(editor_.getLocalPoint(nullptr, screen)));
}

// The card is placed from the real pointer first (so a drag past the canvas origin still measures its full overshoot
// for the patch slide), then pulled back only as far as keeps its grabbed point in view.
juce::Point<int> CanvasEdgeScrollDriver::keepGrabInView(const juce::Component& card) const {
    auto* parent = card.getParentComponent();
    if (parent == nullptr)
        return card.getPosition();
    const auto grab = editor_.getLocalPoint(&card, grabInCard_);
    const auto inside = keepInView(grab);
    if (inside == grab)
        return card.getPosition();
    return card.getPosition() + parent->getLocalPoint(&editor_, inside) - parent->getLocalPoint(&editor_, grab);
}

juce::Point<int> CanvasEdgeScrollDriver::cableEndForTest() const { return editor_.dragCurrentPos; }

void CanvasEdgeScrollDriver::setPointerForTest(std::function<juce::Point<float>()> pointerInCanvasView) {
    testPointer_ = std::move(pointerInCanvasView);
}

// Every drag but a library one starts at a press on the canvas, and the pointer is still there when it arms (the
// begin* call that stores it), so the press is where the pointer is now.
juce::Point<int> CanvasEdgeScrollDriver::pressInView() const { return pointerInView().roundToInt(); }

juce::Point<float> CanvasEdgeScrollDriver::pointerInView() const {
    if (testPointer_ != nullptr)
        return testPointer_();
    const auto screen = lastScreenPosition_ ? *lastScreenPosition_ : synth::ui::pointerPosition().toFloat();
    return editor_.getLocalPoint(nullptr, screen);
}

// The pan stays inside the canvas frame on all four sides: never past the origin (nothing sits left of or above it),
// and never further right or down than the frame's target, so the view stops at its right and bottom border as it does
// at the origin on the left and top. A card drag grows the target ahead of the card (GraphEditor::refreshCanvasFrame),
// so it keeps panning; a cable or a box does not grow it. A view already outside that range is left where it is rather
// than snapped back.
bool CanvasEdgeScrollDriver::panBy(juce::Point<float> d) {
    d = holdDropArea(d);
    const auto frame = editor_.canvasFrame_.target();
    const auto clampAxis = [](float cur, float delta, float viewExtent, float frameExtent) {
        const float lo = std::min(cur, viewExtent - frameExtent);
        const float hi = std::max(cur, 0.0f);
        return std::clamp(cur + delta, lo, hi);
    };
    const auto next = juce::Point<float>(
        clampAxis(editor_.panOffset.x, d.x, (float)editor_.getWidth(), (float)frame.getWidth() * editor_.zoomLevel),
        clampAxis(editor_.panOffset.y, d.y, (float)editor_.getHeight(), (float)frame.getHeight() * editor_.zoomLevel));
    if (next == editor_.panOffset)
        return false;
    editor_.panOffset = next;
    editor_.updateTransform();
    return true;
}

// Whether the pan can still move on an axis in the direction the content would go (`direction` < 0: left or up, which
// reveals the right or bottom); the same range as panBy, probed with a one pixel step.
bool CanvasEdgeScrollDriver::canPan(bool horizontal, float direction) const {
    const auto frame = editor_.canvasFrame_.target();
    const float cur = horizontal ? editor_.panOffset.x : editor_.panOffset.y;
    const float view = (float)(horizontal ? editor_.getWidth() : editor_.getHeight());
    const float extent = (float)(horizontal ? frame.getWidth() : frame.getHeight()) * editor_.zoomLevel;
    const float lo = std::min(cur, view - extent);
    const float hi = std::max(cur, 0.0f);
    return std::clamp(cur + direction, lo, hi) != cur;
}

// The marker shows on each side the pointer is pushing against while the pan cannot go any further that way. The
// target is zero until the pointer has moved toward the edge (EdgeIntent), so a press by the edge shows nothing. A
// cable or a selection box also marks every side of the canvas frame the pointer is beyond (frameSidesPast), at the
// frame's own border: they do not grow the frame, so that is where they stop counting.
void CanvasEdgeScrollDriver::aim(juce::Point<float> target) {
    unsigned sides = 0;
    if (target.x > 0.0f && !canPan(true, 1.0f))
        sides |= CanvasEdgeLimitMarker::Left;
    if (target.x < 0.0f && !canPan(true, -1.0f))
        sides |= CanvasEdgeLimitMarker::Right;
    if (target.y > 0.0f && !canPan(false, 1.0f))
        sides |= CanvasEdgeLimitMarker::Top;
    if (target.y < 0.0f && !canPan(false, -1.0f))
        sides |= CanvasEdgeLimitMarker::Bottom;
    unsigned beyond = 0;
    juce::Rectangle<float> frame;
    if (marksFramePast_ && scroller_.isActive()) {
        frame = frameInView();
        beyond = frameSidesPast(pointerInView(), frame);
    }
    marker_->setSides(sides | beyond, beyond, frame);
}

// The canvas frame (its animated rect) in the view's own coordinates.
juce::Rectangle<float> CanvasEdgeScrollDriver::frameInView() const {
    const auto f = editor_.canvasFrame_.current();
    const auto tl = editor_.getLocalPoint(&editor_.content, f.getTopLeft());
    const auto br = editor_.getLocalPoint(&editor_.content, f.getBottomRight());
    return juce::Rectangle<float>::leftTopRightBottom(tl.x, tl.y, br.x, br.y);
}

unsigned CanvasEdgeScrollDriver::frameSidesPast(juce::Point<float> pointer, juce::Rectangle<float> frame) {
    unsigned sides = 0;
    if (pointer.x < frame.getX())
        sides |= CanvasEdgeLimitMarker::Left;
    if (pointer.y < frame.getY())
        sides |= CanvasEdgeLimitMarker::Top;
    if (pointer.x > frame.getRight())
        sides |= CanvasEdgeLimitMarker::Right;
    if (pointer.y > frame.getBottom())
        sides |= CanvasEdgeLimitMarker::Bottom;
    return sides;
}

// A pointer drag with a landing area scrolls only while some of that area is still hidden in the direction it pans
// from: a pan that reveals the bottom stops once the area's bottom meets the view's bottom, so the pointer held at
// that edge rests on the area's last strip and the view never runs on past it; likewise for the other three sides.
juce::Point<float> CanvasEdgeScrollDriver::holdDropArea(juce::Point<float> d) const {
    if (dropArea_ == nullptr)
        return d;
    const auto area = editor_.getLocalArea(nullptr, dropArea_()).toFloat();
    const auto holdAxis = [](float delta, float lo, float hi, float viewExtent) {
        if (delta < 0.0f) // the content moves up or left: reveals what lies past the far side
            return std::max(delta, std::min(0.0f, viewExtent - hi));
        if (delta > 0.0f)
            return std::min(delta, std::max(0.0f, -lo));
        return delta;
    };
    return {holdAxis(d.x, area.getX(), area.getRight(), (float)editor_.getWidth()),
            holdAxis(d.y, area.getY(), area.getBottom(), (float)editor_.getHeight())};
}

// A real drag event would have arrived at the pointer's position in the card's own space; the card is then handled
// exactly as for a mouse move (ComponentDragger measures from the press, so the grab point is kept).
void CanvasEdgeScrollDriver::beginCardDrag(juce::Component& card, juce::Point<int> pressInCard) {
    dropArea_ = nullptr;
    marksFramePast_ = false; // a card grows the frame ahead of itself
    grabInCard_ = pressInCard;
    juce::Component::SafePointer<juce::Component> safeCard(&card);
    lastScreenPosition_ = card.localPointToGlobal(pressInCard).toFloat(); // the pointer is where the press landed
    scroller_.begin(
        [this, safeCard, pressInCard] {
            auto* c = safeCard.getComponent();
            if (c == nullptr)
                return;
            const GraphDragDropController::ScrollTick tick(editor_.getDragDropController());
            const auto pos = c->getLocalPoint(&editor_, pointerInView());
            const juce::ScopedValueSetter<bool> synthetic(inSyntheticDrag_, true);
            const auto now = juce::Time::getCurrentTime();
            c->mouseDrag(
                juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), pos,
                                 synth::ui::currentModifiers().withFlags(juce::ModifierKeys::leftButtonModifier), 0.0f,
                                 0.0f, 0.0f, 0.0f, 0.0f, c, c, now, pressInCard.toFloat(), now, 1, true));
        },
        [this] { editor_.getDragDropController().flushDragPreview(); }, pressInView());
}

// dragConnection takes a screen position, which is what a real cable drag event hands it.
void CanvasEdgeScrollDriver::beginCableDrag(juce::Point<int> screenPos) {
    dropArea_ = nullptr;
    marksFramePast_ = true; // the cable end follows the pointer anywhere; the frame's border is told
    lastScreenPosition_ = screenPos.toFloat();
    scroller_.begin([this] { editor_.dragConnection(editor_.localPointToGlobal(pointerInView().roundToInt())); },
                    nullptr, pressInView());
}

// Only while the canvas still owns a box or a macro move: a cancelled one must not fall through to the pan branch of
// GraphEditor::mouseDrag, which a synthetic left-button event would otherwise drive.
void CanvasEdgeScrollDriver::beginCanvasDrag() {
    dropArea_ = nullptr;
    if (!editor_.isMarqueeActive() && !(editor_.isMacroChipDragActive() && editor_.isSelectionDragActive()))
        return;
    marksFramePast_ = editor_.isMarqueeActive(); // a macro moved by its chip grows the frame like a card
    scroller_.begin(
        [this] {
            if (!editor_.isMarqueeActive() && !editor_.isMacroChipDragActive())
                return;
            const auto pos = pointerInView().roundToInt();
            const auto now = juce::Time::getCurrentTime();
            editor_.mouseDrag(
                juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), pos.toFloat(),
                                 synth::ui::currentModifiers().withFlags(juce::ModifierKeys::leftButtonModifier), 0.0f,
                                 0.0f, 0.0f, 0.0f, 0.0f, &editor_, &editor_, now, pos.toFloat(), now, 1, true));
        },
        nullptr, pressInView());
}

void CanvasEdgeScrollDriver::notePointerAt(juce::Point<int> screenPos) {
    if (!inSyntheticDrag_)
        lastScreenPosition_ = screenPos.toFloat();
}

void CanvasEdgeScrollDriver::beginPointerDrag(juce::Point<int> screenPos, std::function<bool(juce::Point<int>)> reaim,
                                              std::function<juce::Rectangle<int>()> dropArea) {
    marksFramePast_ = false;
    lastScreenPosition_ = screenPos.toFloat();
    dropArea_ = std::move(dropArea);
    scroller_.begin(
        [this, reaim = std::move(reaim)] {
            const juce::ScopedValueSetter<bool> synthetic(inSyntheticDrag_, true);
            if (!reaim(editor_.localPointToGlobal(pointerInView().roundToInt())))
                end();
        },
        nullptr, pressInView());
}

void CanvasEdgeScrollDriver::beginLibraryDrag(const juce::DragAndDropTarget::SourceDetails& details) {
    dropArea_ = nullptr;
    marksFramePast_ = false;
    libraryDrag_ = details;
    lastScreenPosition_.reset(); // a library drag is the OS drag-and-drop's: the cursor is its pointer
    scroller_.begin(
        [this] {
            if (!libraryDrag_)
                return;
            auto moved = *libraryDrag_;
            moved.localPosition = pointerInView().roundToInt();
            const GraphDragDropController::ScrollTick tick(editor_.getDragDropController());
            editor_.getDragDropController().itemDragMove(moved);
        },
        [this] { editor_.getDragDropController().flushDragPreview(); });
}

// Every drop and release ends the scroll before it reads the preview, so the flush here is what makes the drop use
// the preview for where the pointer is now rather than one a throttled tick skipped (GraphDragDropController).
void CanvasEdgeScrollDriver::end() {
    dropArea_ = nullptr;
    marksFramePast_ = false;
    editor_.getDragDropController().flushDragPreview();
    scroller_.end();
}

} // namespace synth::ui
