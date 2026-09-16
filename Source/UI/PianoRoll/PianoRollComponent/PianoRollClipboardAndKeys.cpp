// PianoRollComponent — the note clipboard (cut/copy/paste/duplicate for the note selection) and
// arrow-key editing (nudge, transpose by semitone/octave/row, select-adjacent-note navigation). The
// class itself is declared in PianoRollComponent.h; sibling PianoRoll<Concern>.cpp units in this
// directory hold the rest (construction/geometry, scale assist, painting, edit tools, audition,
// mouse, zoom).

#include "PianoRollComponent.h"

#include "AppUndoManager.h"
#include "PianoRollInternal.h"
#include <algorithm>
#include <cmath>

namespace synth::ui {

using namespace synth::ui::detail;

//==============================================================================
// ---- Note clipboard ----

std::vector<PianoRollComponent::ClipboardNote> PianoRollComponent::captureSelectionEntries(double& earliestStartOut,
                                                                                           double& spanBeatsOut) const {
    std::vector<ClipboardNote> entries;
    earliestStartOut = 0.0;
    spanBeatsOut = 0.0;
    if (doc_ == nullptr)
        return entries;

    std::vector<synth::MidiNote> notes;
    for (auto id : selection_.getSelected())
        if (const auto* note = doc_->getNote(id))
            notes.push_back(*note);
    if (notes.empty())
        return entries;

    double earliest = notes.front().startBeat;
    double latestEnd = notes.front().startBeat + notes.front().lengthBeats;
    for (const auto& note : notes) {
        earliest = std::min(earliest, note.startBeat);
        latestEnd = std::max(latestEnd, note.startBeat + note.lengthBeats);
    }

    entries.reserve(notes.size());
    for (const auto& note : notes) {
        ClipboardNote entry;
        entry.offsetFromEarliest = note.startBeat - earliest;
        entry.lengthBeats = note.lengthBeats;
        entry.pitch = note.pitch;
        entry.velocity = note.velocity;
        entry.channel = note.channel;
        entry.muted = note.muted;
        entries.push_back(entry);
    }

    earliestStartOut = earliest;
    spanBeatsOut = latestEnd - earliest;
    return entries;
}

bool PianoRollComponent::copySelectedNotes() {
    double earliest = 0.0, span = 0.0;
    auto entries = captureSelectionEntries(earliest, span);
    if (entries.empty())
        return false;
    noteClipboard_ = std::move(entries);
    return true;
}

bool PianoRollComponent::canPasteNotes() const noexcept {
    return !noteClipboard_.empty() && doc_ != nullptr && clipId_.isValid();
}

bool PianoRollComponent::buildPastedNotes(const std::vector<ClipboardNote>& entries, double anchorBeat,
                                          std::vector<synth::MidiNote>& out) const {
    const auto* clip = doc_ != nullptr ? doc_->getClip(clipId_) : nullptr;
    if (clip == nullptr || entries.empty() || !std::isfinite(anchorBeat))
        return false;

    const double clipLength = clip->lengthBeats;
    bool placedAny = false;
    for (const auto& entry : entries) {
        const double start = anchorBeat + entry.offsetFromEarliest;
        if (start < 0.0 || start >= clipLength - kBeatEpsilon)
            continue; // outside the clip: notes only exist inside one
        const double room = clipLength - start;
        if (room < kMinNoteLengthBeats)
            continue; // less than the editor's minimum note left — skipped, never shrunk below it

        synth::MidiNote note;
        note.startBeat = start;
        note.lengthBeats = std::min(entry.lengthBeats, room);
        note.pitch = entry.pitch;
        note.velocity = entry.velocity;
        note.channel = entry.channel;
        note.muted = entry.muted;
        out.push_back(note);
        placedAny = true;
    }
    return placedAny;
}

bool PianoRollComponent::commitPastedNotes(const std::vector<synth::MidiNote>& notes) {
    if (doc_ == nullptr || !clipId_.isValid() || notes.empty())
        return false;

    std::vector<synth::NoteId> added;
    auto mutate = [this, &notes, &added] {
        added.clear();
        for (const auto& note : notes)
            if (const auto id = doc_->addNote(clipId_, note); id.isValid())
                added.push_back(id);
    };
    if (undoManager_)
        undoManager_->recordTimelineChange(*doc_, mutate);
    else
        mutate();

    if (added.empty())
        return false;
    // The pasted block becomes the selection — the user's next gesture is almost always aimed at
    // what they just placed (drag it, transpose it, paste again further along).
    selection_.setSelection(added);
    repaint();
    return true;
}

bool PianoRollComponent::pasteNotesAtPlayhead() {
    if (!canPasteNotes())
        return false;
    const auto* clip = doc_->getClip(clipId_);
    if (clip == nullptr)
        return false;

    // The playhead is an ABSOLUTE timeline beat; notes are clip-relative. A playhead parked
    // outside the edited clip has no meaningful position inside it, so the block goes to the
    // clip's start rather than nowhere.
    double anchor = snappedBeatAt(playheadBeat_ - clip->startBeat);
    if (!(anchor >= 0.0 && anchor < clip->lengthBeats))
        anchor = 0.0;

    std::vector<synth::MidiNote> notes;
    buildPastedNotes(noteClipboard_, anchor, notes);
    return commitPastedNotes(notes);
}

bool PianoRollComponent::duplicateSelectedNotes() {
    double earliest = 0.0, span = 0.0;
    const auto entries = captureSelectionEntries(earliest, span);
    if (entries.empty() || span <= 0.0)
        return false;

    std::vector<synth::MidiNote> notes;
    buildPastedNotes(entries, earliest + span, notes);
    return commitPastedNotes(notes);
}

bool PianoRollComponent::cutSelectedNotes() {
    if (!copySelectedNotes())
        return false;

    const auto ids = selection_.getSelected();
    auto mutate = [this, ids] {
        for (auto id : ids)
            doc_->removeNote(id);
    };
    if (undoManager_)
        undoManager_->recordTimelineChange(*doc_, mutate);
    else
        mutate();

    selection_.clear();
    clearSplitPreview();
    repaint();
    return true;
}

bool PianoRollComponent::selectAllNotes() {
    const auto* clip = doc_ != nullptr ? doc_->getClip(clipId_) : nullptr;
    if (clip == nullptr || clip->notes.empty())
        return false;

    std::vector<synth::NoteId> ids;
    ids.reserve(clip->notes.size());
    for (const auto& note : clip->notes)
        ids.push_back(note.id);
    selection_.setSelection(ids);
    repaint();
    return true;
}

bool PianoRollComponent::repeatSelectedNotes(int count) {
    if (count <= 0)
        return false;
    double earliest = 0.0, span = 0.0;
    const auto entries = captureSelectionEntries(earliest, span);
    if (entries.empty() || span <= 0.0)
        return false;

    std::vector<synth::MidiNote> notes;
    for (int i = 1; i <= count; ++i) {
        // STOP at the first block that lands entirely outside the clip rather than piling the
        // remaining copies onto the last beat — a repeat that runs off the end simply repeats
        // fewer times.
        if (!buildPastedNotes(entries, earliest + span * (double)i, notes))
            break;
    }
    return commitPastedNotes(notes);
}

//==============================================================================
// ---- Arrow-key editing ----

bool PianoRollComponent::nudgeSelectedNotes(int direction) {
    const auto* clip = doc_ != nullptr ? doc_->getClip(clipId_) : nullptr;
    if (clip == nullptr)
        return false;

    std::vector<NoteOrigin> origins;
    double minStart = 0.0, maxEnd = 0.0;
    bool first = true;
    for (auto id : selection_.getSelected()) {
        const auto* note = doc_->getNote(id);
        if (note == nullptr)
            continue;
        origins.push_back({id, note->startBeat, note->lengthBeats, note->pitch, note->velocity});
        const double end = note->startBeat + note->lengthBeats;
        if (first || note->startBeat < minStart)
            minStart = note->startBeat;
        if (first || end > maxEnd)
            maxEnd = end;
        first = false;
    }
    if (origins.empty())
        return false;

    // One grid step (a sixteenth when snap is off — the same floor a free-hand note gets), and ONE
    // shared delta clamped so the whole block stays inside the clip. Clamping per note would
    // silently squash a chord against the boundary instead of stopping it as a unit.
    const double grid = currentGridBeats();
    double delta = (grid > 0.0 ? grid : kMinNoteLengthBeats) * (double)direction;
    delta = std::max(delta, -minStart);
    delta = std::min(delta, clip->lengthBeats - maxEnd);

    auto mutate = [this, origins, delta] {
        for (const auto& origin : origins)
            doc_->moveNote(origin.id, origin.startBeat + delta, origin.pitch);
    };
    if (undoManager_)
        undoManager_->recordTimelineChange(*doc_, mutate);
    else
        mutate();

    repaint();
    return true; // consumed even when the clamp left nothing to do — the key WAS applicable
}

bool PianoRollComponent::transposeSelectedNotes(int semitones) {
    if (doc_ == nullptr || !clipId_.isValid())
        return false;

    std::vector<NoteOrigin> origins;
    int minPitch = 127, maxPitch = 0;
    bool first = true;
    for (auto id : selection_.getSelected()) {
        const auto* note = doc_->getNote(id);
        if (note == nullptr)
            continue;
        origins.push_back({id, note->startBeat, note->lengthBeats, note->pitch, note->velocity});
        if (first || note->pitch < minPitch)
            minPitch = note->pitch;
        if (first || note->pitch > maxPitch)
            maxPitch = note->pitch;
        first = false;
    }
    if (origins.empty())
        return false;

    // Same shared-delta rule as the drag's pitch clamp: the interval between the selected notes is
    // preserved, so a chord transposes as a chord and never collapses at the pitch extremes.
    const int delta = juce::jlimit(-minPitch, 127 - maxPitch, semitones);

    auto mutate = [this, origins, delta] {
        for (const auto& origin : origins)
            doc_->moveNote(origin.id, origin.startBeat, origin.pitch + delta);
    };
    if (undoManager_)
        undoManager_->recordTimelineChange(*doc_, mutate);
    else
        mutate();

    repaint();
    return true;
}

bool PianoRollComponent::transposeSelectedNotesByRow(int rowDelta) {
    if (doc_ == nullptr || !clipId_.isValid() || rowDelta == 0)
        return false;
    const long long totalRows = (long long)visiblePitches_.size();
    if (totalRows <= 0)
        return false;

    std::vector<NoteOrigin> origins;
    long long minRow = 0, maxRow = 0;
    bool first = true;
    for (auto id : selection_.getSelected()) {
        const auto* note = doc_->getNote(id);
        if (note == nullptr)
            continue;
        origins.push_back({id, note->startBeat, note->lengthBeats, note->pitch, note->velocity});
        const long long row = (long long)nearestVisibleRowIndex(note->pitch);
        if (first || row < minRow)
            minRow = row;
        if (first || row > maxRow)
            maxRow = row;
        first = false;
    }
    if (origins.empty())
        return false;

    // ONE shared row delta for the whole selection, clamped so the GROUP stays inside
    // visiblePitches_ — never per-note clamping, which would collapse a chord at either extreme. The
    // exact clamp the Move drag's row half uses.
    const long long delta = std::clamp((long long)rowDelta, -minRow, totalRows - 1 - maxRow);
    if (delta == 0)
        return true; // fully clamped: the key WAS applicable, it simply had nowhere left to go

    auto mutate = [this, origins, delta] {
        for (const auto& origin : origins)
            doc_->moveNote(origin.id, origin.startBeat, rowShiftedPitch(origin.pitch, delta));
    };
    if (undoManager_)
        undoManager_->recordTimelineChange(*doc_, mutate);
    else
        mutate();

    repaint();
    return true;
}

bool PianoRollComponent::selectAdjacentNote(bool forward) {
    const auto* clip = doc_ != nullptr && clipId_.isValid() ? doc_->getClip(clipId_) : nullptr;
    if (clip == nullptr || clip->notes.empty())
        return false;

    // clip->notes IS the canonical order — TimelineDoc maintains (startBeat, pitch, id) on every
    // mutation and re-establishes it on load — so "the note after this one" is literally the next
    // index. Sorting a copy here would be a SECOND definition of that order, free to drift from the
    // doc's; walking the vector cannot.
    const int count = (int)clip->notes.size();
    int anchor = -1;
    for (int i = 0; i < count; ++i) {
        if (!selection_.contains(clip->notes[(std::size_t)i].id))
            continue;
        // Anchor on the edge of the selection we are walking TOWARDS: the last selected note going
        // forward, the first going back. Anchoring on the other edge would step back INTO a
        // multi-selection instead of past it.
        anchor = i;
        if (!forward)
            break;
    }
    if (anchor < 0)
        return false; // nothing selected in this clip: the key falls through to the panel

    const int target = anchor + (forward ? 1 : -1);
    if (target < 0 || target >= count)
        return true; // at the end of the run: selection kept, key still consumed

    const auto& note = clip->notes[(std::size_t)target];
    selection_.setSelection({note.id});
    scrollNoteIntoView(note);
    repaint(); // the selection DID change (target != anchor), so this is state-gated
    return true;
}

void PianoRollComponent::scrollNoteIntoView(const synth::MidiNote& note) {
    const auto* clip = doc_ != nullptr ? doc_->getClip(clipId_) : nullptr;
    if (clip == nullptr || rollView_.pixelsPerBeat <= 0.0)
        return;
    const double gridWidth = (double)std::max(0, getWidth() - leftGutterWidth());
    if (gridWidth <= 0.0)
        return;

    const double visibleBeats = gridWidth / rollView_.pixelsPerBeat;
    const double startAbs = clip->startBeat + note.startBeat;
    const double endAbs = startAbs + note.lengthBeats;

    // MINIMAL scroll, and horizontal only. Off to the left: bring the note's leading edge to the
    // grid's left edge. Off to the right: bring its trailing edge to the right edge — except when
    // the note is wider than the whole view, where std::min picks the leading edge instead (seeing
    // where a note starts beats seeing where it ends). Zoom is never touched: a navigation must not
    // silently reframe the clip.
    //
    // The PITCH scroll is deliberately left alone. Alt+Up/Down is reserved for a future binding, and
    // yanking the vertical view on a horizontal walk would lose the user's place in the roll.
    double first = rollView_.firstVisibleBeat;
    if (startAbs < first)
        first = startAbs;
    else if (endAbs > first + visibleBeats)
        first = std::min(startAbs, endAbs - visibleBeats);

    // setHorizontalView repaints once and notifies the ruler's mapping override; calling it only on
    // a real change is what keeps an on-screen navigation at zero extra repaints. No animation —
    // this is a keyboard jump, not a gesture.
    if (std::abs(first - rollView_.firstVisibleBeat) > kBeatEpsilon)
        setHorizontalView(rollView_.pixelsPerBeat, first);
}

bool PianoRollComponent::isQuantiseEnabled() const {
    // Raw division on purpose: the one-shot quantise works from the CHOSEN grid even while the
    // magnetism switch is off (that is its whole point — clean up notes drawn free-hand).
    if (doc_ == nullptr || !clipId_.isValid() || viewState_.divisionBeatsRaw(currentBeatsPerBar()) <= 0.0)
        return false;
    const auto* clip = doc_->getClip(clipId_);
    return clip != nullptr && !clip->notes.empty();
}

void PianoRollComponent::toggleSnap() {
    viewState_.snapEnabled = !viewState_.snapEnabled;
    repaint(); // the gridlines and the Q button's lit state both follow the switch
    if (onSnapToggled)
        onSnapToggled();
}

void PianoRollComponent::flashQuantiseButton() {
    quantiseFlash_ = true;
    // One shot: timerCallback() stops the timer on its first call. Bounded and confined to the
    // button's own rect — never a running animation (see CLAUDE.md's repaint invariant).
    startTimer(kQuantiseFlashMs);
    repaint(quantiseButtonBounds_);
}

void PianoRollComponent::timerCallback() {
    stopTimer();
    if (!quantiseFlash_)
        return;
    quantiseFlash_ = false;
    repaint(quantiseButtonBounds_);
}

void PianoRollComponent::performQuantise() {
    if (!isQuantiseEnabled())
        return;
    const double grid = viewState_.divisionBeatsRaw(currentBeatsPerBar());

    const auto selectedIds = selection_.getSelected();
    if (selectedIds.empty()) {
        // Nothing selected: quantise every note in the clip — the button's documented "or all notes
        // when nothing is selected" behaviour. quantiseNotes has no note-subset overload, so this is
        // the only case it can serve directly. strength 1.0 = hard snap, matching what "Q" implies.
        auto mutate = [this, grid] { doc_->quantiseNotes(clipId_, grid, 1.0); };
        if (undoManager_)
            undoManager_->recordTimelineChange(*doc_, mutate);
        else
            mutate();
    } else {
        // Selected subset: quantiseNotes can't take one, so this is per-note moveNote in ONE
        // mutation lambda instead (still one undo step).
        auto mutate = [this, grid, selectedIds] {
            for (auto id : selectedIds) {
                const auto* note = doc_->getNote(id);
                if (note == nullptr)
                    continue;
                const double nearestGrid = std::round(note->startBeat / grid) * grid;
                if (nearestGrid != note->startBeat)
                    doc_->moveNote(id, std::max(0.0, nearestGrid), note->pitch);
            }
        };
        if (undoManager_)
            undoManager_->recordTimelineChange(*doc_, mutate);
        else
            mutate();
    }
    // An already-quantised clip mutates nothing, and recordTimelineChange creates no undo step for
    // a no-op — the button still flashes, so the click is never silent.
    repaint();
}

// Alt+Q: quantises selected (or all, if none selected) note LENGTHS to the grid -- the length twin
// of performQuantise() above, which quantises note STARTS. Same selection-vs-no-selection branching,
// same grid source (viewState_.divisionBeatsRaw), same isQuantiseEnabled() gate (it deliberately
// checks grid>0/valid clip/non-empty notes and not the snap toggle, so Alt+Q behaves consistently
// with Q) -- and, like performQuantise(), one undo step regardless of how many notes change.
void PianoRollComponent::performQuantiseLength() {
    if (!isQuantiseEnabled())
        return;
    const double grid = viewState_.divisionBeatsRaw(currentBeatsPerBar());

    const auto selectedIds = selection_.getSelected();
    if (selectedIds.empty()) {
        // Nothing selected: quantise every note's length in the clip — quantiseNoteLengths has no
        // note-subset overload, so this is the only case it can serve directly.
        auto mutate = [this, grid] { doc_->quantiseNoteLengths(clipId_, grid); };
        if (undoManager_)
            undoManager_->recordTimelineChange(*doc_, mutate);
        else
            mutate();
    } else {
        // Selected subset: quantiseNoteLengths can't take one, so this is per-note resizeNote in ONE
        // mutation lambda instead (still one undo step). Floored at one grid unit, exactly like the
        // TimelineDoc helper, so a note can never resize to zero length.
        auto mutate = [this, grid, selectedIds] {
            for (auto id : selectedIds) {
                const auto* note = doc_->getNote(id);
                if (note == nullptr)
                    continue;
                double nearestGrid = std::round(note->lengthBeats / grid) * grid;
                if (nearestGrid < grid)
                    nearestGrid = grid;
                if (nearestGrid != note->lengthBeats)
                    doc_->resizeNote(id, nearestGrid);
            }
        };
        if (undoManager_)
            undoManager_->recordTimelineChange(*doc_, mutate);
        else
            mutate();
    }
    repaint();
}

//==============================================================================
// ---- Simple accessors (moved out of the header — see PianoRollComponent.h for each contract) ----
bool PianoRollComponent::hasNoteSelection() const noexcept { return !selection_.isEmpty(); }
NoteSelectionModel& PianoRollComponent::getSelectionForTest() noexcept { return selection_; }
int PianoRollComponent::getClipboardSizeForTest() const noexcept { return (int)noteClipboard_.size(); }

} // namespace synth::ui
