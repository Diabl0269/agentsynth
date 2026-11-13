#include "UI/Graph/CanvasEdgeScroll/CanvasEdgeScrollDriver.h"

#include "UI/Graph/GraphEditor/GraphEditor.h"

#include <algorithm>

namespace synth::ui {

CanvasEdgeScrollDriver::CanvasEdgeScrollDriver(GraphEditor& editor)
    : editor_(editor)
    , scroller_(CanvasEdgeScroller::Hooks{
          [this] { return editor_.getLocalBounds(); }, [this] { return pointerInView(); },
          [this](juce::Point<float> d) { return panBy(d); },
          [this] {
              // The mouse source's own button state as well as the hardware's: test mode injects
              // its presses into the window's peer, which the hardware never sees.
              return testPointer_ != nullptr || juce::Desktop::getInstance().getMainMouseSource().isDragging() ||
                     juce::ModifierKeys::getCurrentModifiersRealtime().isAnyMouseButtonDown();
          }}) {
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

void CanvasEdgeScrollDriver::setPointerForTest(std::function<juce::Point<float>()> pointerInCanvasView) {
    testPointer_ = std::move(pointerInCanvasView);
}

juce::Point<float> CanvasEdgeScrollDriver::pointerInView() const {
    if (testPointer_ != nullptr)
        return testPointer_();
    const auto screen = lastScreenPosition_ ? *lastScreenPosition_
                                            : juce::Desktop::getInstance().getMainMouseSource().getScreenPosition();
    return editor_.getLocalPoint(nullptr, screen);
}

// The pan stays inside the canvas: never past the origin (nothing sits left of or above it), and never further right
// or down than the content extends, which a card drag grows ahead of itself (GraphEditor::refreshCanvasFrame).
// A view already outside that range is left where it is rather than snapped back.
bool CanvasEdgeScrollDriver::panBy(juce::Point<float> d) {
    d = holdDropArea(d);
    const auto clampAxis = [](float cur, float delta, float viewExtent, float contentExtent) {
        const float lo = std::min(cur, viewExtent - contentExtent);
        const float hi = std::max(cur, 0.0f);
        return std::clamp(cur + delta, lo, hi);
    };
    const auto next = juce::Point<float>(clampAxis(editor_.panOffset.x, d.x, (float)editor_.getWidth(),
                                                   (float)editor_.content.getWidth() * editor_.zoomLevel),
                                         clampAxis(editor_.panOffset.y, d.y, (float)editor_.getHeight(),
                                                   (float)editor_.content.getHeight() * editor_.zoomLevel));
    if (next == editor_.panOffset)
        return false;
    editor_.panOffset = next;
    editor_.updateTransform();
    return true;
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
            c->mouseDrag(juce::MouseEvent(
                juce::Desktop::getInstance().getMainMouseSource(), pos,
                juce::ModifierKeys::getCurrentModifiersRealtime().withFlags(juce::ModifierKeys::leftButtonModifier),
                0.0f, 0.0f, 0.0f, 0.0f, 0.0f, c, c, now, pressInCard.toFloat(), now, 1, true));
        },
        [this] { editor_.getDragDropController().flushDragPreview(); });
}

// dragConnection takes a screen position, which is what a real cable drag event hands it.
void CanvasEdgeScrollDriver::beginCableDrag(juce::Point<int> screenPos) {
    dropArea_ = nullptr;
    lastScreenPosition_ = screenPos.toFloat();
    scroller_.begin([this] { editor_.dragConnection(editor_.localPointToGlobal(pointerInView().roundToInt())); });
}

// Only while the canvas still owns a box or a macro move: a cancelled one must not fall through to the pan branch of
// GraphEditor::mouseDrag, which a synthetic left-button event would otherwise drive.
void CanvasEdgeScrollDriver::beginCanvasDrag() {
    dropArea_ = nullptr;
    if (!editor_.isMarqueeActive() && !(editor_.isMacroChipDragActive() && editor_.isSelectionDragActive()))
        return;
    scroller_.begin([this] {
        if (!editor_.isMarqueeActive() && !editor_.isMacroChipDragActive())
            return;
        const auto pos = pointerInView().roundToInt();
        const auto now = juce::Time::getCurrentTime();
        editor_.mouseDrag(juce::MouseEvent(
            juce::Desktop::getInstance().getMainMouseSource(), pos.toFloat(),
            juce::ModifierKeys::getCurrentModifiersRealtime().withFlags(juce::ModifierKeys::leftButtonModifier), 0.0f,
            0.0f, 0.0f, 0.0f, 0.0f, &editor_, &editor_, now, pos.toFloat(), now, 1, true));
    });
}

void CanvasEdgeScrollDriver::notePointerAt(juce::Point<int> screenPos) {
    if (!inSyntheticDrag_)
        lastScreenPosition_ = screenPos.toFloat();
}

void CanvasEdgeScrollDriver::beginPointerDrag(juce::Point<int> screenPos, std::function<bool(juce::Point<int>)> reaim,
                                              std::function<juce::Rectangle<int>()> dropArea) {
    lastScreenPosition_ = screenPos.toFloat();
    dropArea_ = std::move(dropArea);
    scroller_.begin([this, reaim = std::move(reaim)] {
        const juce::ScopedValueSetter<bool> synthetic(inSyntheticDrag_, true);
        if (!reaim(editor_.localPointToGlobal(pointerInView().roundToInt())))
            end();
    });
}

void CanvasEdgeScrollDriver::beginLibraryDrag(const juce::DragAndDropTarget::SourceDetails& details) {
    dropArea_ = nullptr;
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
    editor_.getDragDropController().flushDragPreview();
    scroller_.end();
}

} // namespace synth::ui
