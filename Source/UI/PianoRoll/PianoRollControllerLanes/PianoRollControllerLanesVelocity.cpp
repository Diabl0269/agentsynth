// PianoRollControllerLanes — the velocity lane's three gestures (declared in
// PianoRollControllerLanes.h): drag a BAR (that note's velocity follows the pointer), FREEHAND
// (every note whose start the pointer passes takes the value under it) and Shift+drag LINE (a
// straight ramp across every note between press and release). With a note selection, freehand and
// line only touch selected notes; a bar drag always edits the bar it grabbed. Preview lives in
// previewVelocities_; mouse-up commits it as ONE TimelineDoc::setNoteVelocities call.

#include "PianoRollControllerLanes.h"

#include "UI/PianoRoll/PianoRollComponent/PianoRollComponent.h"
#include <algorithm>
#include <cmath>

namespace synth::ui {

std::vector<synth::NoteId> PianoRollControllerLanes::selectedNoteIds() const {
    return roll_.getSelection().getSelected();
}

// The notes whose bar is under x: the NEAREST start within kBarHitPx wins, and every note sharing
// that start (a chord) comes along — unless some of them are selected, in which case only those.
std::vector<synth::NoteId> PianoRollControllerLanes::barNotesAt(int x) const {
    const auto* clip = openClip();
    if (clip == nullptr)
        return {};
    std::optional<double> bestStart;
    int bestDistance = kBarHitPx + 1;
    for (const auto& note : clip->notes) {
        if (note.startBeat >= clip->lengthBeats)
            break;
        const int distance = std::abs(xForClipBeat(note.startBeat) - x);
        if (distance < bestDistance) {
            bestDistance = distance;
            bestStart = note.startBeat;
        }
    }
    if (!bestStart)
        return {};
    std::vector<synth::NoteId> all;
    std::vector<synth::NoteId> selected;
    for (const auto& note : clip->notes) {
        if (note.startBeat != *bestStart)
            continue;
        all.push_back(note.id);
        if (roll_.getSelection().contains(note.id))
            selected.push_back(note.id);
    }
    return selected.empty() ? all : selected;
}

void PianoRollControllerLanes::beginVelocityGesture(juce::Point<int> pos, bool line) {
    hoveredBarBeat_.reset();
    const int value = juce::jlimit(1, 127, (int)std::lround(valueForY(pos.y)));
    if (line) {
        gesture_ = Gesture::VelocityLine;
        anchorBeat_ = clipBeatAtX(pos.x, false);
        anchorValue_ = valueForY(pos.y);
        dragVelocityGesture(pos);
    } else if (auto bar = barNotesAt(pos.x); !bar.empty()) {
        gesture_ = Gesture::VelocityBar;
        gestureNotes_ = std::move(bar);
        std::map<std::int64_t, int> next;
        for (const auto& id : gestureNotes_)
            next[id.value] = value;
        setPreviewVelocities(std::move(next));
    } else {
        gesture_ = Gesture::VelocityFreehand;
        dragVelocityGesture(pos);
    }
    repaint();
}

// Freehand interpolates between the PREVIOUS and the current pointer position, so a fast drag that
// jumps several bars per event still sets every one of them (and sets them along the line the hand
// actually travelled, not to the value at the far end).
void PianoRollControllerLanes::dragVelocityGesture(juce::Point<int> pos) {
    const auto* clip = openClip();
    if (clip == nullptr)
        return;
    const double value = valueForY(pos.y);
    auto next = previewVelocities_;
    switch (gesture_) {
    case Gesture::VelocityBar:
        for (const auto& id : gestureNotes_)
            next[id.value] = juce::jlimit(1, 127, (int)std::lround(value));
        break;
    case Gesture::VelocityLine:
        next.clear();
        for (const auto& [id, velocity] :
             lanes::velocityLine(*clip, selectedNoteIds(), anchorBeat_, anchorValue_, clipBeatAtX(pos.x, false), value))
            next[id.value] = velocity;
        break;
    case Gesture::VelocityFreehand:
        for (const auto& [id, velocity] : lanes::velocityLine(*clip, selectedNoteIds(), clipBeatAtX(lastPos_.x, false),
                                                              valueForY(lastPos_.y), clipBeatAtX(pos.x, false), value))
            next[id.value] = velocity;
        break;
    default:
        return;
    }
    setPreviewVelocities(std::move(next));
    repaint();
}

// The one writer of previewVelocities_. Every note whose previewed velocity CHANGED (appeared,
// moved or dropped out) gets its rect in the roll repainted — the note body is coloured by the
// preview through resolveNoteColour — and nothing else in the roll is touched.
void PianoRollControllerLanes::setPreviewVelocities(std::map<std::int64_t, int> next) {
    std::vector<synth::NoteId> changed;
    for (const auto& [id, velocity] : next) {
        const auto it = previewVelocities_.find(id);
        if (it == previewVelocities_.end() || it->second != velocity)
            changed.push_back(synth::NoteId{id});
    }
    for (const auto& [id, velocity] : previewVelocities_)
        if (next.count(id) == 0)
            changed.push_back(synth::NoteId{id});
    previewVelocities_ = std::move(next);
    roll_.repaintNotesForLanes(changed);
}

std::optional<int> PianoRollControllerLanes::previewVelocityFor(synth::NoteId id) const {
    const auto it = previewVelocities_.find(id.value);
    if (it == previewVelocities_.end())
        return std::nullopt;
    return it->second;
}

void PianoRollControllerLanes::endVelocityGesture() {
    std::vector<std::pair<synth::NoteId, int>> velocities;
    velocities.reserve(previewVelocities_.size());
    for (const auto& [id, velocity] : previewVelocities_)
        velocities.emplace_back(synth::NoteId{id}, velocity);
    gesture_ = Gesture::None; // before the commit: its timelineChanged repaints from the doc
    setPreviewVelocities({});
    gestureNotes_.clear();
    commitVelocities(velocities);
    repaint();
}

} // namespace synth::ui
