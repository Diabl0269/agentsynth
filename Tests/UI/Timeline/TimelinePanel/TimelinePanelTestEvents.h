#pragma once

// TimelinePanelTestEvents.h
//
// Mouse-event helpers and ruler constants for the TimelinePanel tests, split out of
// TimelinePanelTestFixture.h so tests that drive a bare TimelinePanelComponent need not pull in
// MainComponent.h (FRO307). Header-only; not registered in Tests/CMakeLists.txt.

#include <juce_gui_basics/juce_gui_basics.h>

// ============================================================================
// Hand-built MouseEvent helpers, same pattern as Tests/UI/Graph/GraphEditor/GraphEditorViewportTests.cpp/
// MinimapComponentTests.cpp — no OS mouse source exists headlessly, but MouseInputSource is
// copyable and Desktop always exposes one. mouseWasDragged is the constructor's own bool (JUCE
// stores it verbatim, see MouseEvent::mouseWasDraggedSinceMouseDown()) rather than anything
// derived from real mouse motion, so it is fully under the caller's control here. Used across the
// ruler, marker, track-header, resize, zoom/snap/scroll and authoring-gesture test files.
// ============================================================================

inline juce::MouseEvent makeTimelineMouseEvent(juce::Component& comp, juce::Point<float> position,
                                               juce::ModifierKeys mods, bool mouseWasDragged,
                                               juce::Point<float> mouseDownPos) {
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), position, mods, 0.0f, 0.0f, 0.0f, 0.0f,
                            0.0f, &comp, &comp, juce::Time::getCurrentTime(), mouseDownPos,
                            juce::Time::getCurrentTime(), 1, mouseWasDragged);
}

inline juce::MouseEvent makeClickEvent(juce::Component& comp, juce::Point<float> position,
                                       juce::ModifierKeys mods = juce::ModifierKeys()) {
    return makeTimelineMouseEvent(comp, position, mods, false, position);
}

inline juce::MouseEvent makeDragEvent(juce::Component& comp, juce::Point<float> position, juce::Point<float> anchorPos,
                                      juce::ModifierKeys mods = juce::ModifierKeys()) {
    return makeTimelineMouseEvent(comp, position, mods, true, anchorPos);
}

// The ruler is 24 px tall in every test that uses these, so its zone split sits at y = 12: y < 12
// is the loop (top) zone, y >= 12 the playhead (bottom) zone.
constexpr float kLoopZoneY = 5.0f;
constexpr float kPlayheadZoneY = 18.0f;
