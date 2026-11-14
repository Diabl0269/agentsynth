// EQCurveHoldState.h -- the point-editing state of EQCurveComponent: the shared selection model and box select and the
// shared key hold with where the bands it moves started. Private to EQCurveGlide.cpp (which builds it) and
// EQCurveKeyGlide.cpp (which runs it), so the curve's own header stays out of KeyHold.h's reach.
#pragma once

#include "EQCurveComponent.h"
#include "UI/Layout/KeyGlide/KeyHold.h"
#include "UI/Layout/PointEditing/PointBoxSelect.h"
#include "UI/Layout/PointEditing/PointSelectionModel.h"
#include <utility>
#include <vector>

struct EQCurveComponent::HoldState {
    explicit HoldState(juce::Component& owner)
        : box(owner)
        , hold(owner) {}
    /** The curve's selection (band slots; the cursor is the band the keys act on) and its box select. */
    static constexpr synth::ui::PointSelectionRules kRules{
        synth::ui::PointSelectionRules::FirstStep::firstPoint, false,
        synth::ui::PointSelectionRules::Extend::rangeFromAnchor,
        synth::ui::PointSelectionRules::SelectAllCursor::keepSelectedElseLast};
    synth::ui::PointSelectionModel<int> selection;
    synth::ui::PointBoxSelect<int> box;
    synth::ui::KeyHold hold;
    /** (slot, (frequency Hz, gain dB)) of every band the hold moves, as they were when it began. */
    std::vector<std::pair<int, juce::Point<float>>> start;
    /** One tap step of each axis: octaves and dB (a fifth finer with Shift). */
    float unitOctaves = 0.0f;
    float unitDb = 0.0f;
    bool gestureOpen = false;
};
