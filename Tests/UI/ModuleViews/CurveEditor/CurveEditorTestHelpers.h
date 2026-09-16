// Shared fixture helpers for the CurveEditor test suites: an envelope-shaped CurveModel builder
// (the fixed origin/attack-peak/hold-end/sustain/release-end topology the envelope card will use)
// and real juce::MouseEvent synthesis (same pattern as Tests/UI/PianoRoll/PianoRollTestHelpers.h).
#pragma once

#include "UI/ModuleViews/CurveEditor/CurveEditorComponent.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <limits>

namespace synth::ui::test {

struct EnvelopeShape {
    double attack = 0.005;
    double hold = 0.0;
    double decay = 0.300;
    float sustain = 0.4f;
    double release = 0.500;
    float attackBend = 0.0f;
    float decayBend = 0.0f;
    float releaseBend = 0.0f;
};

/** Node indices for `buildEnvelopeModel`'s fixed topology. */
enum EnvelopeNodeIndex { kOrigin = 0, kAttackPeak = 1, kHoldEnd = 2, kSustain = 3, kReleaseEnd = 4 };
/** Segment indices: attack (0->1), hold (1->2), decay (2->3), release (3->4). */
enum EnvelopeSegmentIndex { kAttackSeg = 0, kHoldSeg = 1, kDecaySeg = 2, kReleaseSeg = 3 };

inline CurveModel buildEnvelopeModel(const EnvelopeShape& shape = {}) {
    CurveModel model(CurveMode::Fixed);

    std::vector<CurveNode> nodes(5);
    nodes[kOrigin] = CurveNode{0.0, 0.0f, false, false};
    nodes[kAttackPeak] = CurveNode{shape.attack, 1.0f, true, false};
    nodes[kHoldEnd] = CurveNode{shape.attack + shape.hold, 1.0f, true, false};
    nodes[kSustain] = CurveNode{shape.attack + shape.hold + shape.decay, shape.sustain, true, true};
    nodes[kReleaseEnd] = CurveNode{shape.attack + shape.hold + shape.decay + shape.release, 0.0f, true, false};
    model.setNodes(nodes);

    model.setBend(kAttackSeg, shape.attackBend);
    model.setBendable(kHoldSeg, false); // the hold plateau is pinned start==end==1, never bendable
    model.setBend(kDecaySeg, shape.decayBend);
    model.setBend(kReleaseSeg, shape.releaseBend);
    return model;
}

inline juce::MouseEvent makeCurveMouseEvent(juce::Component& comp, juce::Point<float> position, juce::ModifierKeys mods,
                                            bool mouseWasDragged, juce::Point<float> mouseDownPos) {
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), position, mods, 0.0f, 0.0f, 0.0f, 0.0f,
                            0.0f, &comp, &comp, juce::Time::getCurrentTime(), mouseDownPos,
                            juce::Time::getCurrentTime(), 1, mouseWasDragged);
}

inline juce::MouseEvent curveLeftClick(juce::Component& comp, juce::Point<float> pos) {
    return makeCurveMouseEvent(comp, pos, juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), false, pos);
}

inline juce::MouseEvent curveLeftDrag(juce::Component& comp, juce::Point<float> pos, juce::Point<float> anchor) {
    return makeCurveMouseEvent(comp, pos, juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), true, anchor);
}

inline juce::MouseEvent curveDoubleClick(juce::Component& comp, juce::Point<float> pos) {
    return makeCurveMouseEvent(comp, pos, juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), false, pos);
}

} // namespace synth::ui::test
