#pragma once

// ViewportDragFollowEvents.h -- builds the checklist's Surface (ViewportDragFollow.h) for a drag whose real mouse
// events go to one component inside a juce::Viewport (a list, a row's grip, a column's header). The press point stays
// where the pointer went down on screen; every event is rebuilt in the target's own coordinates from a position along
// the scroll axis, because the target moves with the drag and the list scrolls under the pointer.
// Header-only; not registered in Tests/CMakeLists.txt.

#include "ViewportDragFollow.h"

namespace viewport_drag_follow {

inline juce::MouseEvent leftButtonEvent(juce::Component& target, juce::Point<float> local, juce::Point<float> down,
                                        bool dragged) {
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), local,
                            juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                            &target, &target, juce::Time::getCurrentTime(), down, juce::Time::getCurrentTime(), 1,
                            dragged);
}

/** Where the events go and what the test reads back. `cross` is the pointer's fixed coordinate across the scroll axis
 *  (viewport coordinates); `target` is looked up again on every event, since a drop can rebuild the components. */
struct DragTarget {
    std::shared_ptr<void> keepAlive;
    juce::Viewport* viewport = nullptr;
    bool horizontal = false;
    std::function<juce::Component*()> target;
    float cross = 0.0f;
    float pressAt = 0.0f;
    std::function<float()> draggedPosition;
    std::function<int()> commits;
    std::function<bool()> escape;
};

inline Surface surfaceOn(const DragTarget& t) {
    auto* viewport = t.viewport;
    const bool horizontal = t.horizontal;
    const auto inViewport = [horizontal, cross = t.cross](float along) {
        return horizontal ? juce::Point<float>(along, cross) : juce::Point<float>(cross, along);
    };
    const auto send = [=](float along, bool dragged, bool up) {
        auto* component = t.target();
        const auto down = component->getLocalPoint(viewport, inViewport(t.pressAt));
        const auto at = component->getLocalPoint(viewport, inViewport(along));
        if (up)
            component->mouseUp(leftButtonEvent(*component, at, down, dragged));
        else if (dragged)
            component->mouseDrag(leftButtonEvent(*component, at, down, true));
        else
            component->mouseDown(leftButtonEvent(*component, at, down, false));
    };
    Surface s;
    s.keepAlive = t.keepAlive;
    s.extent = [viewport, horizontal] {
        return horizontal ? viewport->getMaximumVisibleWidth() : viewport->getMaximumVisibleHeight();
    };
    s.viewPosition = [viewport, horizontal] {
        return horizontal ? viewport->getViewPositionX() : viewport->getViewPositionY();
    };
    s.press = [=](float along) { send(along, false, false); };
    s.drag = [=](float along) { send(along, true, false); };
    s.release = [=](float along) { send(along, true, true); };
    s.escape = t.escape;
    s.draggedPosition = t.draggedPosition;
    s.commits = t.commits;
    s.pressAt = t.pressAt;
    return s;
}

// The row a press can land on with its middle clear of both edge zones and room to be lifted.
template <class MiddleOf>
inline int firstRowClearOfTheEdges(int rowCount, int extent, const MiddleOf& middleOf,
                                   const std::function<bool(int)>& usable) {
    for (int row = 0; row < rowCount; ++row)
        if (usable(row) && middleOf(row) >= 50 && middleOf(row) <= extent - 80)
            return row;
    return -1;
}

} // namespace viewport_drag_follow
