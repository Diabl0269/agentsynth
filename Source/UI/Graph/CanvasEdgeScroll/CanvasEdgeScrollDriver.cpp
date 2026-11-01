#include "UI/Graph/CanvasEdgeScroll/CanvasEdgeScrollDriver.h"

#include "UI/Graph/GraphEditor/GraphEditor.h"

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

void CanvasEdgeScrollDriver::noteIfOnCanvas(const juce::MouseEvent& e) {
    if (e.eventComponent == &editor_ || e.eventComponent == &editor_.content)
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

// A real drag event would have arrived at the pointer's position in the card's own space; the card is then handled
// exactly as for a mouse move (ComponentDragger measures from the press, so the grab point is kept).
void CanvasEdgeScrollDriver::beginCardDrag(juce::Component& card, juce::Point<int> pressInCard) {
    juce::Component::SafePointer<juce::Component> safeCard(&card);
    lastScreenPosition_ = card.localPointToGlobal(pressInCard).toFloat(); // the pointer is where the press landed
    scroller_.begin([this, safeCard, pressInCard] {
        auto* c = safeCard.getComponent();
        if (c == nullptr)
            return;
        const auto pos = c->getLocalPoint(&editor_, pointerInView());
        const juce::ScopedValueSetter<bool> synthetic(inSyntheticDrag_, true);
        const auto now = juce::Time::getCurrentTime();
        c->mouseDrag(juce::MouseEvent(
            juce::Desktop::getInstance().getMainMouseSource(), pos,
            juce::ModifierKeys::getCurrentModifiersRealtime().withFlags(juce::ModifierKeys::leftButtonModifier), 0.0f,
            0.0f, 0.0f, 0.0f, 0.0f, c, c, now, pressInCard.toFloat(), now, 1, true));
    });
}

// dragConnection takes a screen position, which is what a real cable drag event hands it.
void CanvasEdgeScrollDriver::beginCableDrag(juce::Point<int> screenPos) {
    lastScreenPosition_ = screenPos.toFloat();
    scroller_.begin([this] { editor_.dragConnection(editor_.localPointToGlobal(pointerInView().roundToInt())); });
}

// Only while the canvas still owns a box or a macro move: a cancelled one must not fall through to the pan branch of
// GraphEditor::mouseDrag, which a synthetic left-button event would otherwise drive.
void CanvasEdgeScrollDriver::beginCanvasDrag() {
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

void CanvasEdgeScrollDriver::beginLibraryDrag(const juce::DragAndDropTarget::SourceDetails& details) {
    libraryDrag_ = details;
    lastScreenPosition_.reset(); // a library drag is the OS drag-and-drop's: the cursor is its pointer
    scroller_.begin([this] {
        if (!libraryDrag_)
            return;
        auto moved = *libraryDrag_;
        moved.localPosition = pointerInView().roundToInt();
        editor_.getDragDropController().itemDragMove(moved);
    });
}

} // namespace synth::ui
