// DrumMachineComponent: the paint stroke. Pressing an empty cell and dragging adds a hit to every cell the pointer
// passes; the same stroke started on a hit removes the hits it passes. The first cell decides add or remove, and the
// stroke follows the pointer to any row. Hits are previewed (they grow in with a faint outline) and written on release
// as ONE undo step. Shift+Right/Left is the keyboard form: it carries the focused cell's hit (or its emptiness) along.

#include "DrumMachineComponent.h"

#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <algorithm>
#include <cmath>

namespace synth::ui {

namespace {
double strokeNowMs() { return juce::Time::getMillisecondCounterHiRes(); }
constexpr int kMaxSamplesPerMove = 4096;
constexpr float kStrokeOutlineAlpha = 0.55f;
} // namespace

void DrumMachineComponent::beginStroke(const Cell& first, bool erase, int velocity, juce::Point<float> pos) {
    gesture_ = {};
    gesture_.kind = Gesture::Kind::Stroke;
    gesture_.erase = erase;
    gesture_.strokeVelocity = velocity;
    gesture_.lastPos = pos;
    visitStrokeCell(first);
}

// Interpolates from the last pointer sample to `pos` in steps of half a cell, so a fast drag skips no cell.
void DrumMachineComponent::extendStroke(juce::Point<float> pos) {
    const auto region = gridRegion().toFloat();
    const auto inner = region.reduced(0.5f);
    pos = {juce::jlimit(inner.getX(), inner.getRight(), pos.x), juce::jlimit(inner.getY(), inner.getBottom(), pos.y)};
    const auto from = gesture_.lastPos;
    gesture_.lastPos = pos;
    float minRow = 1.0e6f;
    for (const auto& slot : slots_)
        minRow = std::min(minRow, static_cast<float>(rowHeightOf(slot)));
    const float stepPx = static_cast<float>(std::max(1.0, beatToX(getStepBeats()) - beatToX(0.0)));
    const float pitch = std::max(1.0f, std::min(stepPx, minRow) * 0.5f);
    const int samples =
        juce::jlimit(1, kMaxSamplesPerMove, static_cast<int>(std::ceil(from.getDistanceFrom(pos) / pitch)));
    for (int i = 1; i <= samples; ++i) {
        const auto p = from + (pos - from) * (static_cast<float>(i) / static_cast<float>(samples));
        if (const auto cell = cellAt(p.roundToInt()))
            visitStrokeCell(*cell);
    }
}

// One cell the stroke passes. Cells the stroke cannot change (a ghost, an empty cell of an erase stroke, a hit of an
// add stroke) are skipped; a new one is previewed and, when it adds a hit, auditioned (at most one per 40 ms).
void DrumMachineComponent::visitStrokeCell(const Cell& cell) {
    const int pitch = getLanePitch(cell.row);
    if (pitch < 0 || ghostSourceStep(cell.row, cell.step) >= 0 || isPastRowEnd(pitch, cell.step))
        return;
    const auto key = drumgrid::cellKey(pitch, cell.step);
    const auto found = cells_.find(key);
    const bool occupied = found != cells_.end();
    if (occupied != gesture_.erase || gesture_.strokeKeys.count(key) > 0)
        return;
    gesture_.strokeKeys.insert(key);
    gesture_.strokeCells.emplace_back(pitch, cell.step);
    beginHitMotion(key, !gesture_.erase, gesture_.erase ? found->second.velocity : gesture_.strokeVelocity);
    repaint(rectForKey(key));
    const double now = strokeNowMs();
    if (!gesture_.erase && now - gesture_.lastAuditionMs >= kStrokeAuditionGapMs) {
        gesture_.lastAuditionMs = now;
        audition(pitch, gesture_.strokeVelocity);
    }
}

// Writes the stroke as one undo step. The previewed motions carry on into the real cells, so nothing grows twice.
void DrumMachineComponent::commitStroke(const Gesture& stroke) {
    if (stroke.strokeCells.empty() || clip() == nullptr) {
        repaint();
        return;
    }
    const auto id = clipId_;
    std::vector<synth::MidiNote> added;
    std::vector<synth::NoteId> removed;
    for (const auto& [pitch, step] : stroke.strokeCells) {
        if (stroke.erase) {
            const auto it = cells_.find(drumgrid::cellKey(pitch, step));
            if (it != cells_.end())
                removed.insert(removed.end(), it->second.ids.begin(), it->second.ids.end());
            continue;
        }
        synth::MidiNote note;
        note.startBeat = static_cast<double>(step) * getStepBeats();
        note.lengthBeats = getStepBeats();
        note.pitch = pitch;
        note.velocity = juce::jlimit(1, 127, stroke.strokeVelocity);
        added.push_back(note);
    }
    record([this, id, added, removed] {
        for (const auto& note : added)
            doc_->addNote(id, note);
        for (const auto noteId : removed)
            doc_->removeNote(noteId);
    });
    settledKeys_ = stroke.strokeKeys;
    refreshFromDoc();
    settledKeys_.clear();
    const auto count = static_cast<int>(stroke.strokeCells.size());
    announce((stroke.erase ? "Removed " : "Added ") + juce::String(count) + (count == 1 ? " hit" : " hits"));
}

// The faint outline a previewed stroke cell keeps until release.
void DrumMachineComponent::paintStrokeOutline(juce::Graphics& g, juce::Rectangle<float> area, float radius) const {
    g.setColour(synth::theme::themeOf(*this).colors.accent.withAlpha(kStrokeOutlineAlpha));
    g.drawRoundedRectangle(area.reduced(0.75f), radius, 1.5f);
}

// Escape: the previewed hits go and nothing is written.
void DrumMachineComponent::cancelStroke(const Gesture& stroke) {
    for (const auto key : stroke.strokeKeys) {
        hitMotions_.erase(key);
        repaint(rectForKey(key));
    }
}

// Shift+Right/Left: the focus moves a step and carries the cell's state onto the cell it lands on. From a hit that
// paints a hit (with its loudness) there; from an empty cell it erases the hit there. Each press is one undo step.
bool DrumMachineComponent::carryFocus(int dStep) {
    if (slots_.empty() || !clipId_.isValid())
        return false;
    const int pitch = getLanePitch(focus_.row);
    const int target = juce::jlimit(0, std::max(0, getStepCount() - 1), focus_.step + dStep);
    if (target == focus_.step)
        return true;
    const int velocity = getCellVelocity(focus_.row, focus_.step);
    setFocusCell({focus_.row, target});
    const bool ghost = ghostSourceStep(focus_.row, target) >= 0;
    if (pitch >= 0 && !ghost && !isPastRowEnd(pitch, target)) {
        const bool occupied = cells_.count(drumgrid::cellKey(pitch, target)) > 0;
        if (velocity > 0 && !occupied)
            addHit(pitch, target, velocity);
        else if (velocity == 0 && occupied)
            removeHits(pitch, target);
    }
    announceFocus();
    return true;
}

} // namespace synth::ui
