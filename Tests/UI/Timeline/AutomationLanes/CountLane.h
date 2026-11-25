#pragma once

// CountLane.h -- an open lane with the Draw tool, Snap off and a periodic pen, plus the real-event helpers the cycle
// count tests drive it with (Option held while dragging, the bracket keys). Header-only.

#include "AutomationLanesTestFixture.h"
#include "UI/ModuleViews/ShapeEditor/CycleCountStepper.h"
#include "UI/Timeline/AutomationLanes/LaneShapes/LaneShapeGenerator.h"

namespace automation_lanes_test {

inline constexpr float kStep = synth::ui::CycleCountStepper::kStepPx;
inline const juce::KeyPress kOpen('[');
inline const juce::KeyPress kClose(']');

// An open Bass track with one [0, 100] lane and Snap off, the Draw tool and a Sine chosen.
struct CountLane : LanesPanel {
    synth::TrackId bass;
    synth::LaneId lane;
    synth::ui::AutomationLaneEditor* editor = nullptr;
    juce::Point<float> from, anchor;

    explicit CountLane(synth::ui::DrawShape shape = synth::ui::DrawShape::Sine, bool snapOff = true) {
        bass = doc.addTrack(synth::TrackKind::Midi, "Bass");
        lane = addLane(bass, "cutoff");
        panel.setTrackAutomationExpanded(bass, true);
        editor = panel.laneEditorForTest(lane);
        panel.getViewState().snapEnabled = !snapOff;
        panel.setActiveTool(synth::ui::EditTool::Draw);
        panel.setDrawShape(shape);
    }

    const synth::AutomationLane& theLane() const { return *doc.getLane(lane); }
    synth::ui::AutomationLaneShapeGesture& gesture() { return editor->getShapeGesture(); }
    float xAt(double beat) { return (float)panel.getViewState().beatToX(beat); }
    float yAt(double value) { return (float)editor->valueToY(value); }

    // A plain drag from `beat0` to `beat1` and no release yet.
    void press(double beat0, double beat1) {
        from = {xAt(beat0), yAt(80.0)};
        anchor = from;
        editor->mouseDown(makeClickEvent(*editor, from, leftButton()));
        editor->mouseDrag(makeDragEvent(*editor, {xAt(beat1), yAt(20.0)}, from, leftButton()));
        end_ = {xAt(beat1), yAt(20.0)};
    }
    // Option down with the pointer `steps` count steps above where it went down (the first call is where it goes down).
    void option(float steps) {
        if (!optionDown_) {
            optionDown_ = true;
            optionY_ = end_.y;
        }
        const juce::Point<float> p{end_.x, optionY_ - steps * kStep};
        editor->mouseDrag(makeDragEvent(*editor, p, from, leftButton(juce::ModifierKeys::altModifier)));
    }
    void release() { editor->mouseUp(makeDragEvent(*editor, end_, from, leftButton())); }
    bool key(const juce::KeyPress& k) { return editor->keyPressed(k); }
    // Cycles the last-drawn sine stamp holds, read from its points.
    int drawnSineCycles() const { return ((int)theLane().points.size() - 1) / synth::ui::kSinePointsPerCycle; }
    juce::String chip() { return gesture().getChipText(); }

private:
    juce::Point<float> end_;
    bool optionDown_ = false;
    float optionY_ = 0.0f;
};

inline juce::String cyclesChip(int cycles, const char* length = "1 bar") {
    return juce::String(cycles) + " cycles" + juce::String(juce::CharPointer_UTF8(" \xc2\xb7 ")) + length;
}

} // namespace automation_lanes_test
