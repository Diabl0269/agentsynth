#pragma once

// Shared fixture and helpers for the clip-lane EDIT TOOLS half of the clip-editing test suite
// (Tests/Timeline/TimelineClipEditing/TimelineClipEditing{Tools,Drag,Context}Tests.cpp) — Split,
// Glue, Erase, Mute and Draw driven through synth::ui::TimelineClipLaneArea with synthetic mouse
// events, the Select tool's Alt-copy and cross-track drags, the Split tool's hover preview repaint
// budget, and the inline rename's commit path. Every test configures the view state (snap
// division, snap switch, zoom, scroll) EXPLICITLY: a tool's whole behaviour is defined against the
// grid, so inheriting a default — let alone a persisted user setting — would make the file's
// results machine-dependent.
// Header-only; not compiled on its own and not registered in Tests/CMakeLists.txt.

#include "AppUndoManager.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "UI/Timeline/ClipSelectionModel.h"
#include "UI/Timeline/EditTool.h"
#include "UI/Timeline/TimelineClipLaneArea/TimelineClipLaneArea.h"
#include "UI/Timeline/TimelineViewState.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

using synth::ClipId;
using synth::TimelineDoc;
using synth::TrackKind;
using synth::ui::EditTool;
using synth::ui::TimelineClipLaneArea;
using synth::ui::TimelineViewState;

// 40 px/beat, 1-beat snap grid, nothing scrolled, no vertical zoom — every coordinate in the tool
// tests is derived from these, and every one of them is set explicitly (see the file header).
struct ToolLaneFixture {
    TimelineDoc doc;
    TimelineViewState state;
    synth::ui::ClipSelectionModel selection;
    AppUndoManager undo;
    TimelineClipLaneArea lane{state, selection};

    ToolLaneFixture() {
        state.pixelsPerBeat = 40.0;
        state.firstVisibleBeat = 0.0;
        state.snap = TimelineViewState::Snap::Quarter; // a quarter note == 1 beat
        state.snapEnabled = true;
        state.rowHeightScale = 1.0;
        state.trackScrollY = 0.0;
        lane.setTimelineDoc(&doc);
        lane.setUndoManager(&undo);
        lane.setSize(1200, 400);
    }

    // The vertical centre of track row `index`, headless (no themed LookAndFeel => the row height
    // is TimelineTrackHeaderComponent::kRowHeight).
    float rowCentreY(int index) const {
        const int rowHeight = lane.getRowHeight();
        return (float)(index * rowHeight + rowHeight / 2);
    }
    float rowHeightF() const { return (float)lane.getRowHeight(); }
};

// Hand-built MouseEvents, same pattern as TimelineClipLane/*.cpp/Tests/UI/Graph/GraphEditor/*.cpp — no OS
// mouse source exists headlessly, and `mouseWasDragged` is the constructor's own bool.
inline juce::MouseEvent makeToolMouseEvent(juce::Component& comp, juce::Point<float> position, juce::ModifierKeys mods,
                                           bool mouseWasDragged, juce::Point<float> mouseDownPos) {
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), position, mods, 0.0f, 0.0f, 0.0f, 0.0f,
                            0.0f, &comp, &comp, juce::Time::getCurrentTime(), mouseDownPos,
                            juce::Time::getCurrentTime(), 1, mouseWasDragged);
}

inline juce::MouseEvent toolClick(juce::Component& comp, juce::Point<float> pos, int extraFlags = 0) {
    return makeToolMouseEvent(comp, pos, juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier | extraFlags), false,
                              pos);
}

inline juce::MouseEvent toolDrag(juce::Component& comp, juce::Point<float> pos, juce::Point<float> anchor,
                                 int extraFlags = 0) {
    return makeToolMouseEvent(comp, pos, juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier | extraFlags), true,
                              anchor);
}

inline juce::MouseEvent hoverAt(juce::Component& comp, juce::Point<float> pos) {
    return makeToolMouseEvent(comp, pos, juce::ModifierKeys(), false, pos);
}

inline juce::Point<float> clipCentre(const TimelineClipLaneArea& lane, ClipId id) {
    const auto rect = lane.getClipRect(id);
    return {(float)rect.getCentreX(), (float)rect.getCentreY()};
}

// One press-release with no movement — what "clicking with a tool" means for Split/Glue/Erase/
// Mute (they all act on the press; the release is what proves it does not act twice).
inline void clickWithTool(TimelineClipLaneArea& lane, juce::Point<float> pos, int extraFlags = 0) {
    lane.mouseDown(toolClick(lane, pos, extraFlags));
    lane.mouseUp(toolClick(lane, pos, extraFlags));
}
