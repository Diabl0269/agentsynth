#pragma once

// AutomationLanesTestFixture.h -- a bare TimelinePanelComponent over its own doc and undo manager,
// plus the hand-built event helpers the automation-lane tests drive it with (no OS event queue in
// a headless test binary: a real event is the component's own mouse/key entry point called with a
// MouseEvent built for it). Header-only; not registered in Tests/CMakeLists.txt.

#include "../TimelinePanel/TimelinePanelTestEvents.h"
#include "AppUndoManager.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "UI/Timeline/TimelinePanelComponent/TimelinePanelComponent.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace automation_lanes_test {

inline juce::ModifierKeys leftButton(int extra = 0) {
    return juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier | extra);
}

// A full click through a button's own mouseDown/mouseUp. juce::Button narrows both to protected,
// so they are reached through the Component base (still dispatching to the Button's overrides).
inline void clickButton(juce::Button& button) {
    auto& comp = static_cast<juce::Component&>(button);
    const auto centre = comp.getLocalBounds().getCentre().toFloat();
    comp.mouseDown(makeClickEvent(comp, centre, leftButton()));
    comp.mouseUp(makeClickEvent(comp, centre, leftButton()));
}

// Press at `from`, drag through `steps` evenly spaced points, release at `to` -- all in `comp`'s
// own coordinates, the way JUCE delivers a real drag to the component that took the press.
inline void dragAcross(juce::Component& comp, juce::Point<float> from, juce::Point<float> to, int steps = 8,
                       juce::ModifierKeys mods = leftButton()) {
    comp.mouseDown(makeClickEvent(comp, from, mods));
    for (int i = 1; i <= steps; ++i) {
        const auto t = (float)i / (float)steps;
        comp.mouseDrag(makeDragEvent(comp, from + (to - from) * t, from, mods));
    }
    comp.mouseUp(makeDragEvent(comp, to, from, mods));
}

inline const juce::PopupMenu::Item* findMenuItem(const juce::PopupMenu& menu, const juce::String& text) {
    juce::PopupMenu::MenuItemIterator it(menu, true);
    while (it.next())
        if (it.getItem().text == text)
            return &it.getItem();
    return nullptr;
}

// Doc and undo manager are declared before the panel: ~TimelinePanelComponent de-registers from the
// doc, so the doc must outlive it.
struct LanesPanel {
    synth::TimelineDoc doc;
    AppUndoManager undo;
    synth::ui::TimelinePanelComponent panel;

    LanesPanel() {
        panel.setTimelineDoc(&doc);
        panel.setUndoManager(&undo);
        panel.setSize(1200, 600);
        panel.setVisible(true); // getComponentAt() only descends into visible components
        auto& view = panel.getViewState();
        view.pixelsPerBeat = 40.0;
        view.firstVisibleBeat = 0.0;
        view.snap = synth::ui::TimelineViewState::Snap::Quarter;
    }

    // A lane over [0, 100] (default 50), so a 40 px row maps 2.5 units per pixel.
    synth::LaneId addLane(synth::TrackId track, const juce::String& paramId) {
        synth::AutomationLane::RangeSnapshot range;
        range.minValue = 0.0f;
        range.maxValue = 100.0f;
        range.defaultValue = 50.0f;
        return doc.addLane(track, "node-" + paramId, paramId, range);
    }

    int indexOf(synth::TrackId track) const {
        const auto& tracks = doc.getTracks();
        for (int i = 0; i < (int)tracks.size(); ++i)
            if (tracks[(size_t)i].id == track)
                return i;
        return -1;
    }

    // What a real click at `p` (panel coordinates) would land on, after every hitTest on the way.
    juce::Component* componentAt(juce::Point<int> p) { return panel.getComponentAt(p); }
};

} // namespace automation_lanes_test
