#include "UI/Graph/CanvasEdgeScroll/CanvasEdgeScrollDriver.h"

#include "UI/Graph/GraphEditor/GraphEditor.h"

namespace synth::ui {

CanvasEdgeScrollDriver::CanvasEdgeScrollDriver(GraphEditor& editor)
    : editor_(editor)
    , scroller_(
          CanvasEdgeScroller::Hooks{[this] { return editor_.getLocalBounds(); }, [this] { return pointerInView(); },
                                    [this](juce::Point<float> d) { return panBy(d); },
                                    [this] {
                                        return testPointer_ != nullptr ||
                                               juce::ModifierKeys::getCurrentModifiersRealtime().isAnyMouseButtonDown();
                                    }}) {}

void CanvasEdgeScrollDriver::setPointerForTest(std::function<juce::Point<float>()> pointerInCanvasView) {
    testPointer_ = std::move(pointerInCanvasView);
}

juce::Point<float> CanvasEdgeScrollDriver::pointerInView() const {
    if (testPointer_ != nullptr)
        return testPointer_();
    return editor_.getLocalPoint(nullptr, juce::Desktop::getInstance().getMainMouseSource().getScreenPosition());
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
    scroller_.begin([this, safeCard, pressInCard] {
        auto* c = safeCard.getComponent();
        if (c == nullptr)
            return;
        const auto pos = c->getLocalPoint(&editor_, pointerInView());
        const auto now = juce::Time::getCurrentTime();
        c->mouseDrag(juce::MouseEvent(
            juce::Desktop::getInstance().getMainMouseSource(), pos,
            juce::ModifierKeys::getCurrentModifiersRealtime().withFlags(juce::ModifierKeys::leftButtonModifier), 0.0f,
            0.0f, 0.0f, 0.0f, 0.0f, c, c, now, pressInCard.toFloat(), now, 1, true));
    });
}

void CanvasEdgeScrollDriver::beginLibraryDrag(const juce::DragAndDropTarget::SourceDetails& details) {
    libraryDrag_ = details;
    scroller_.begin([this] {
        if (!libraryDrag_)
            return;
        auto moved = *libraryDrag_;
        moved.localPosition = pointerInView().roundToInt();
        editor_.getDragDropController().itemDragMove(moved);
    });
}

} // namespace synth::ui
