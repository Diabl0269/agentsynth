// PianoRollVelocityLane — gestures: which drag a press starts (stick / pen / ramp), the live
// preview each one builds, Escape-cancel, and the one commit on release. Every value it computes
// goes through VelocityLaneMath; every edit leaves through the Host's onPreview/onCommit/onCancel.

#include "PianoRollVelocityLane.h"

#include "VelocityLaneMath.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace synth::ui {

namespace {
std::vector<velocitylane::StickPoint> toPoints(const std::vector<PianoRollVelocityLane::Stick>& sticks,
                                               const PianoRollVelocityLane& lane) {
    std::vector<velocitylane::StickPoint> points;
    points.reserve(sticks.size());
    for (const auto& s : sticks)
        points.push_back({s.id, s.x, lane.yForVelocity(s.velocity)});
    return points;
}
} // namespace

synth::NoteId PianoRollVelocityLane::stickUnder(juce::Point<float> pos) const {
    const int gutter = gutterWidth();
    std::vector<Stick> visible;
    for (const auto& s : currentSticks())
        if (s.x >= gutter)
            visible.push_back(s);
    const auto picked = velocitylane::pickStick(toPoints(visible, *this), pos, kStickHitPx);
    return picked ? visible[*picked].id : synth::NoteId{};
}

void PianoRollVelocityLane::mouseDown(const juce::MouseEvent& e) {
    if (gesture_ != Gesture::None)
        finishGesture(false); // a press can never stack on a gesture whose release was lost
    if (!e.mods.isLeftButtonDown())
        return;
    grabKeyboardFocus();
    if (e.position.x < (float)gutterWidth())
        return; // the scale gutter is not a place to edit
    beginGesture(e.position, e.mods.isShiftDown());
}

void PianoRollVelocityLane::mouseDrag(const juce::MouseEvent& e) {
    if (gesture_ != Gesture::None)
        updateGesture(e.position);
}

void PianoRollVelocityLane::mouseUp(const juce::MouseEvent&) {
    if (gesture_ != Gesture::None)
        finishGesture(true);
}

// Precedence, first match wins: the Draw tool makes every press a pen stroke (the pencil means
// "draw"); Shift means a straight ramp from the press point; a press within kStickHitPx of a stick
// grabs that stick; anything else is a freehand pen stroke. Whichever notes are selected when the
// press lands are the only ones a pen or ramp may touch — with no selection, every note is fair.
void PianoRollVelocityLane::beginGesture(juce::Point<float> pos, bool shift) {
    origins_ = currentSticks();
    const bool anySelected = std::any_of(origins_.begin(), origins_.end(), [](const Stick& s) { return s.selected; });
    eligible_.clear();
    for (const auto& s : origins_)
        if (!anySelected || s.selected)
            eligible_.push_back(s);
    preview_.clear();
    grabbed_ = {};
    anchor_ = lastPos_ = pos;

    const bool drawTool = host_.isDrawToolActive && host_.isDrawToolActive();
    const auto hit = (drawTool || shift) ? synth::NoteId{} : stickUnder(pos);
    if (drawTool)
        gesture_ = Gesture::Pen;
    else if (shift)
        gesture_ = Gesture::Ramp;
    else if (hit.isValid()) {
        gesture_ = Gesture::Stick;
        grabbed_ = hit;
        const auto it = std::find_if(origins_.begin(), origins_.end(), [hit](const Stick& s) { return s.id == hit; });
        grabbedRelative_ = it != origins_.end() && it->selected;
    } else
        gesture_ = Gesture::Pen;

    // The press itself already sets a value: a click on a stick or on the strip is an edit, not
    // just the start of one.
    updateGesture(pos);
}

void PianoRollVelocityLane::updateGesture(juce::Point<float> pos) {
    switch (gesture_) {
    case Gesture::Stick:
        applyStickDrag(pos);
        break;
    case Gesture::Pen:
        applyLine(lastPos_, pos, /*accumulate=*/true);
        break;
    case Gesture::Ramp:
        applyLine(anchor_, pos, /*accumulate=*/false);
        break;
    case Gesture::None:
        return;
    }
    lastPos_ = pos;
    publishPreview();
}

// An unselected stick is set ABSOLUTELY from the pointer's y, alone. A selected stick drags the
// whole selection by one shared delta (the pointer's travel in velocity units since the press, so
// nothing jumps on the press itself), each note clamped to [1, 127] on its own.
void PianoRollVelocityLane::applyStickDrag(juce::Point<float> pos) {
    readout_ = grabbed_;
    preview_.clear();
    if (!grabbedRelative_) {
        preview_[grabbed_] = velocityForY(pos.y);
        return;
    }
    const int delta = velocityForY(pos.y) - velocityForY(anchor_.y);
    for (const auto& s : origins_)
        if (s.selected)
            preview_[s.id] = velocitylane::clampVelocity(s.velocity + delta);
}

// The pen accumulates segment by segment (lastPos_ -> pos), so a fast stroke that skips pixels
// between two mouse events still sets every stick it crossed; the ramp redraws from the anchor each
// time, so shrinking it back gives the notes it no longer spans their original values again.
void PianoRollVelocityLane::applyLine(juce::Point<float> from, juce::Point<float> to, bool accumulate) {
    if (!accumulate)
        preview_.clear();
    const auto hits = velocitylane::lineVelocities(toPoints(eligible_, *this), from, to, plotTop(), plotBottom());
    int bestDx = std::numeric_limits<int>::max();
    for (const auto& [id, velocity] : hits) {
        preview_[id] = velocity;
        for (const auto& s : eligible_) {
            const int dx = std::abs(s.x - (int)std::lround(to.x));
            if (s.id == id && dx < bestDx) {
                bestDx = dx;
                readout_ = id;
            }
        }
    }
}

void PianoRollVelocityLane::publishPreview() {
    if (host_.onPreview)
        host_.onPreview(preview_);
    repaint();
}

// Commits only the notes whose value actually changed — a gesture that ends where it started writes
// nothing, which is what keeps a click-and-release from costing an undo step.
void PianoRollVelocityLane::finishGesture(bool commit) {
    std::vector<std::pair<synth::NoteId, int>> changes;
    if (commit) {
        for (const auto& [id, velocity] : preview_) {
            const auto it = std::find_if(origins_.begin(), origins_.end(), [id](const Stick& s) { return s.id == id; });
            if (it != origins_.end() && it->velocity != velocity)
                changes.emplace_back(id, velocity);
        }
    }
    gesture_ = Gesture::None;
    preview_.clear();
    origins_.clear();
    eligible_.clear();
    grabbed_ = {};
    readout_ = hovered_;
    if (!changes.empty()) {
        if (host_.onCommit)
            host_.onCommit(changes);
    } else if (host_.onCancel) {
        host_.onCancel();
    }
    repaint();
}

bool PianoRollVelocityLane::cancelGesture() {
    if (gesture_ == Gesture::None)
        return false;
    finishGesture(false);
    return true;
}

// Hover only moves the readout, and only repaints when the hovered stick changes.
void PianoRollVelocityLane::mouseMove(const juce::MouseEvent& e) {
    const auto id = stickUnder(e.position);
    if (id == hovered_)
        return;
    hovered_ = id;
    if (gesture_ == Gesture::None)
        readout_ = id;
    repaint();
}

void PianoRollVelocityLane::mouseExit(const juce::MouseEvent&) {
    if (!hovered_.isValid())
        return;
    hovered_ = {};
    if (gesture_ == Gesture::None)
        readout_ = {};
    repaint();
}

bool PianoRollVelocityLane::keyPressed(const juce::KeyPress& key) {
    if (key == juce::KeyPress::escapeKey)
        return cancelGesture();
    return false;
}

} // namespace synth::ui
