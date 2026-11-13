#pragma once

// Shared by the drum machine's groove and lane tests: a grid opened on an 8-beat clip, with helpers that add hits and
// toggle the lane chips the way a click does.

#include "../PianoRoll/PianoRollTestHelpers.h"
#include "UI/DrumMachine/DrumMachineComponent.h"
#include "UI/Layout/FadeVisibility.h"
#include "UI/Layout/ReducedMotion.h"

using synth::ui::DrumHitLane;
using synth::ui::DrumLaneKind;
using synth::ui::DrumMachineComponent;

namespace drumtest {

// The motion tests pin Animations to Full: the mode is process-wide, so an earlier test (or the machine's Reduce
// Motion) would otherwise decide whether a flash or a lane motion runs at all.
struct AnimationModeGuard {
    explicit AnimationModeGuard(synth::ui::AnimationMode mode) { synth::ui::setAnimationMode(mode); }
    ~AnimationModeGuard() { synth::ui::setAnimationMode(synth::ui::AnimationMode::followSystem); }
};

struct GrooveFixture {
    TimelineDoc doc;
    AppUndoManager undo;
    DrumMachineComponent grid;
    synth::TrackId track;
    ClipId clip;

    explicit GrooveFixture(int height = 360) {
        synth::ui::FadeVisibility::setAnimateOffScreenForTest(false);
        track = doc.addTrack(TrackKind::Midi, "Drums");
        clip = doc.addClip(track, 0.0, 8.0, "Beat 1");
        grid.setTimelineDoc(&doc);
        grid.setUndoManager(&undo);
        grid.setSize(1000, height);
        grid.openClip(clip);
    }

    const synth::Clip& c() { return *doc.getClip(clip); }

    void addHits(int pitch, std::initializer_list<int> steps, int velocity = 100) {
        for (const int step : steps) {
            auto note = makeNote(0.25 * step, pitch, 0.25);
            note.velocity = velocity;
            doc.addNote(clip, note);
        }
        grid.refreshFromDoc();
        undo.clearUndoHistory();
    }

    /** A click on the lane's chip with real mouse events: the button's own click path (toggle, then onToggle). */
    void clickChip(DrumLaneKind kind) {
        juce::Component& chip = *grid.getLaneChips().getChip(static_cast<int>(kind));
        const auto p = centreOf(chip.getLocalBounds());
        chip.mouseEnter(hover(chip, p));
        chip.mouseDown(leftClick(chip, p));
        chip.mouseUp(leftClick(chip, p));
        chip.mouseExit(hover(chip, p));
    }

    void showLane(DrumLaneKind kind) {
        if (!grid.isLaneShown(kind))
            clickChip(kind);
        grid.finishMotionsForTest();
    }

    void showLoudness() { showLane(DrumLaneKind::Loudness); }

    std::vector<int> velocities(int pitch) {
        std::vector<int> out;
        for (const auto& n : c().notes)
            if (n.pitch == pitch)
                out.push_back(n.velocity);
        return out;
    }
};

/** A point in a lane over hit `hitIndex`, `fractionFromBottom` of the way up its bars area. */
inline juce::Point<float> inLane(DrumHitLane& lane, int hitIndex, float fractionFromBottom) {
    const auto bar = lane.barBounds(hitIndex);
    const float y = static_cast<float>(lane.getHeight() - DrumHitLane::kBarInsetPx) -
                    fractionFromBottom * static_cast<float>(lane.getHeight() - 2 * DrumHitLane::kBarInsetPx);
    return {static_cast<float>(bar.getCentreX()), y};
}

} // namespace drumtest
