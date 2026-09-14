// PianoRollComponent — editing gestures (drag-create/move/resize preview + commit), the edit-tool
// verbs (split/glue/erase/mute/draw), the split-tool's hover preview, and per-tool mouse cursors.
// The class itself is declared in PianoRollComponent.h; sibling PianoRoll<Concern>.cpp units in
// this directory hold the rest (construction/geometry, scale assist, painting, audition, clipboard,
// mouse, zoom).

#include "PianoRollComponent.h"

#include "AppUndoManager.h"
#include "PianoRollInternal.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Timeline/ToolCursors.h"
#include <algorithm>
#include <cmath>
#include <memory>
#include <set>

namespace synth::ui {

using namespace synth::ui::detail;

namespace {
// The themed icon each tool's cursor is rendered from (IconLibrary's table is ordered
// independently of EditTool, so the mapping is spelled out rather than cast).
synth::theme::Icon iconForTool(EditTool tool) noexcept {
    using synth::theme::Icon;
    switch (tool) {
    case EditTool::Select:
        return Icon::ToolSelect;
    case EditTool::Split:
        return Icon::ToolSplit;
    case EditTool::Glue:
        return Icon::ToolGlue;
    case EditTool::Erase:
        return Icon::ToolErase;
    case EditTool::Mute:
        return Icon::ToolMute;
    case EditTool::Draw:
        return Icon::ToolDraw;
    }
    return Icon::ToolSelect;
}
} // namespace

//==============================================================================
// ---- Editing gestures ----

void PianoRollComponent::clampToClipWindow(double& start, double& length) const {
    const auto* clip = doc_ != nullptr ? doc_->getClip(clipId_) : nullptr;
    const double clipLength = clip != nullptr ? clip->lengthBeats : 0.0;
    start = juce::jlimit(0.0, clipLength, start);
    length = juce::jlimit(0.0, clipLength - start, length);
}

bool PianoRollComponent::computeNewNoteAnchor(juce::Point<int> pos, bool floorToGrid, double& startOut,
                                              double& lengthOut, int& pitchOut) const {
    const auto* clip = doc_ != nullptr ? doc_->getClip(clipId_) : nullptr;
    if (clip == nullptr)
        return false;

    // The new note is exactly ONE snap division long: quantise 1 bar -> a 1-bar note, 1/4 -> a
    // quarter. Snap Off has no division, so it falls back to the finest grid unit.
    const double grid = currentGridBeats();
    const double length = grid > 0.0 ? grid : kMinNoteLengthBeats;
    const double rawStart = xToBeat((double)pos.x) - clip->startBeat;
    // Nearest division for the double-click (aiming at a line), the containing CELL for the Draw
    // tool's pencil — see the declaration for why the two differ.
    double start = rawStart;
    if (grid > 0.0)
        start = floorToGrid ? std::floor(rawStart / grid) * grid : snappedBeatAt(rawStart);
    // Snapping up past the clip's end would leave no room at all — step back one division instead
    // of silently creating nothing.
    if (grid > 0.0 && start >= clip->lengthBeats)
        start = std::max(0.0, std::floor((clip->lengthBeats - kBeatEpsilon) / grid) * grid);

    double clampedLength = length;
    clampToClipWindow(start, clampedLength);
    if (clampedLength <= 0.0)
        return false; // no room left inside the clip

    startOut = start;
    lengthOut = clampedLength;
    pitchOut = juce::jlimit(0, 127, pitchForY(pos.y));
    return true;
}

void PianoRollComponent::commitNewNote(double startBeat, double lengthBeats, int pitch) {
    if (doc_ == nullptr || !clipId_.isValid() || lengthBeats <= 0.0)
        return;

    synth::NoteId newId;
    auto mutate = [this, startBeat, lengthBeats, pitch, &newId] {
        synth::MidiNote note;
        note.startBeat = startBeat;
        note.lengthBeats = lengthBeats;
        note.pitch = pitch;
        note.velocity = 100;
        note.channel = 1;
        newId = doc_->addNote(clipId_, note);
    };
    if (undoManager_)
        undoManager_->recordTimelineChange(*doc_, mutate);
    else
        mutate();

    if (newId.isValid())
        selection_.setSelection({newId});
    repaint();
}

void PianoRollComponent::createNoteAt(juce::Point<int> pos) {
    double start = 0.0, length = 0.0;
    int pitch = 60;
    if (!computeNewNoteAnchor(pos, false, start, length, pitch))
        return;
    commitNewNote(start, length, pitch);
}

void PianoRollComponent::beginMoveOrResize(const NoteHit& hit, juce::Point<int> pos) {
    mouseDownPos_ = pos;
    activeNote_ = hit.id;

    if (hit.onRightEdge) {
        if (const auto* note = doc_->getNote(hit.id)) {
            dragMode_ = DragMode::Resize;
            resizeOriginalLength_ = note->lengthBeats;
            previewLength_ = resizeOriginalLength_;
            previewLengthDelta_ = 0.0;

            // Snapshot every SELECTED note's origin, exactly like the Move branch below, so a resize
            // grabbed inside a multi-selection trims the whole group by ONE shared length delta
            // (11.1). By the time we get here mouseDown has already made the grabbed note part of
            // the selection (it replaces the selection with just that note when it wasn't in it), so
            // "the selection" IS the right set in both cases — the fallback below only covers a
            // caller that reached this function some other way.
            resizeNotes_.clear();
            if (selection_.contains(hit.id)) {
                for (auto id : selection_.getSelected())
                    if (const auto* selected = doc_->getNote(id))
                        resizeNotes_.push_back(
                            {id, selected->startBeat, selected->lengthBeats, selected->pitch, selected->velocity});
            } else {
                resizeNotes_.push_back({hit.id, note->startBeat, note->lengthBeats, note->pitch, note->velocity});
            }
        }
        return;
    }

    // Move: snapshot every SELECTED note's origin (not just the one grabbed) so a multi-selection
    // moves together by one shared beat delta + one shared semitone delta — the same reasoning
    // TimelineClipLaneArea::mouseDown's Move branch documents for clips.
    dragMode_ = DragMode::Move;
    dragNotes_.clear();
    for (auto id : selection_.getSelected())
        if (const auto* note = doc_->getNote(id))
            dragNotes_.push_back({id, note->startBeat, note->lengthBeats, note->pitch, note->velocity});
    previewDeltaBeats_ = 0.0;
    previewDeltaPitch_ = 0;
}

void PianoRollComponent::beginVelocityScrub(juce::Point<int> pos) {
    mouseDownPos_ = pos;
    dragMode_ = DragMode::VelocityScrub;
    dragNotes_.clear();
    for (auto id : selection_.getSelected())
        if (const auto* note = doc_->getNote(id))
            dragNotes_.push_back({id, note->startBeat, note->lengthBeats, note->pitch, note->velocity});
    previewDeltaVelocity_ = 0;
}

void PianoRollComponent::beginMarquee(juce::Point<int> anchor, bool additive) {
    dragMode_ = DragMode::Marquee;
    marqueeAdditive_ = additive;
    marqueeAnchor_ = anchor;
    marqueeRect_ = juce::Rectangle<int>(anchor, anchor);
    marqueeBaseSelection_ = additive ? selection_.getSelected() : std::vector<synth::NoteId>{};
    if (!additive)
        selection_.clear();
    repaint();
}

void PianoRollComponent::updateMarquee(juce::Point<int> current) {
    marqueeRect_ = juce::Rectangle<int>(marqueeAnchor_, current);
    auto hits = noteHitTestMarquee(marqueeRect_, collectNoteRects());

    if (marqueeAdditive_) {
        std::set<synth::NoteId> merged(marqueeBaseSelection_.begin(), marqueeBaseSelection_.end());
        merged.insert(hits.begin(), hits.end());
        selection_.setSelection({merged.begin(), merged.end()});
    } else {
        selection_.setSelection(hits);
    }
    repaint();
}

void PianoRollComponent::endMarquee() {
    marqueeRect_ = {};
    marqueeBaseSelection_.clear();
    marqueeAdditive_ = false;
    repaint();
}

//==============================================================================
// ---- Edit tools ----

void PianoRollComponent::setActiveTool(EditTool tool) {
    if (activeTool_ == tool)
        return;
    activeTool_ = tool;

    // Any gesture already in flight belonged to the OLD tool — finishing it under the new one
    // would commit an edit the user has just said they no longer want to make.
    dragMode_ = DragMode::None;
    pendingEmptyClick_ = false;
    dragNotes_.clear();
    resizeNotes_.clear();
    resizeUnquantized_ = false;
    moveUnquantized_ = false;
    cmdToggleNote_ = {};
    cmdToggleWasSelected_ = false;
    previewDeltaBeats_ = 0.0;
    previewDeltaPitch_ = 0;
    previewDeltaVelocity_ = 0;
    previewLengthDelta_ = 0.0;
    drawLengthBeats_ = 0.0;
    clearSplitPreview();
    autoScrollTimer_.stopTimer(); // the auto-scroll timer's drag just got cancelled too
    // The cancelled gesture's preview note goes with it — this is a drag that will never see a
    // mouseUp, which is exactly the no-stuck-note case stopAudition exists for.
    stopAudition();
    endKeysColumnPress();

    applyToolCursor();
    repaint();
}

void PianoRollComponent::handleToolMouseDown(juce::Point<int> pos) {
    auto hit = hitTestNote(pos);

    switch (activeTool_) {
    case EditTool::Split:
        if (hit)
            performSplit(hit->id, pos);
        return;
    case EditTool::Glue:
        if (hit)
            performGlue(hit->id);
        return;
    case EditTool::Erase:
        if (hit)
            performErase(hit->id);
        return;
    case EditTool::Mute:
        if (hit)
            performMuteToggle(hit->id);
        return;
    case EditTool::Draw: {
        if (hit)
            return; // the pencil never redraws over a note that is already there
        double start = 0.0, length = 0.0;
        int pitch = 60;
        if (!computeNewNoteAnchor(pos, true, start, length, pitch))
            return;
        // Armed, not committed: the note exists only as a preview until mouseUp, so a drag can
        // still change its length and an abandoned gesture costs no undo step.
        dragMode_ = DragMode::DrawNew;
        mouseDownPos_ = pos;
        drawStartBeat_ = start;
        drawLengthBeats_ = length;
        drawPitch_ = pitch;
        repaint();
        return;
    }
    case EditTool::Select:
        return; // never routed here — mouseDown keeps the whole Select gesture table inline
    }
}

std::optional<double> PianoRollComponent::splitBeatFor(const synth::MidiNote& note, int x) const {
    const auto* clip = doc_ != nullptr ? doc_->getClip(clipId_) : nullptr;
    if (clip == nullptr)
        return std::nullopt;

    const double cut = snappedBeatAt(xToBeat((double)x) - clip->startBeat);
    const double noteEnd = note.startBeat + note.lengthBeats;
    if (cut - note.startBeat < kMinNoteLengthBeats - kBeatEpsilon)
        return std::nullopt;
    if (noteEnd - cut < kMinNoteLengthBeats - kBeatEpsilon)
        return std::nullopt;
    return cut;
}

void PianoRollComponent::performSplit(synth::NoteId id, juce::Point<int> pos) {
    const auto* found = doc_ != nullptr ? doc_->getNote(id) : nullptr;
    if (found == nullptr)
        return;
    // COPY the note before mutating: resizeNote re-positions it inside the clip's sorted vector,
    // so the pointer above is only valid until the first write.
    const synth::MidiNote original = *found;

    const auto cut = splitBeatFor(original, pos.x);
    if (!cut)
        return; // the snapped cut is not strictly inside the note — a no-op, and no undo step

    const double leftLength = *cut - original.startBeat;
    const double rightLength = original.startBeat + original.lengthBeats - *cut;
    const double cutBeat = *cut;

    // Both halves in ONE undo step: a split the user has to undo twice is a split that looks
    // broken. The right half inherits everything the left keeps — pitch, velocity, channel and
    // `muted` — because a split divides a note, it does not author a new one.
    auto mutate = [this, id, leftLength, rightLength, cutBeat, original] {
        doc_->resizeNote(id, leftLength);
        synth::MidiNote right;
        right.startBeat = cutBeat;
        right.lengthBeats = rightLength;
        right.pitch = original.pitch;
        right.velocity = original.velocity;
        right.channel = original.channel;
        right.muted = original.muted;
        doc_->addNote(clipId_, right);
    };
    if (undoManager_)
        undoManager_->recordTimelineChange(*doc_, mutate);
    else
        mutate();

    // Selection is deliberately untouched: the left half keeps the id it had, so a selected note
    // stays selected and the new right half starts unselected — the same shape as a clip split.
    clearSplitPreview();
    repaint();
}

std::optional<synth::NoteId> PianoRollComponent::glueCandidateFor(const synth::MidiNote& note) const {
    const auto* clip = doc_ != nullptr ? doc_->getClip(clipId_) : nullptr;
    if (clip == nullptr)
        return std::nullopt;

    const double noteEnd = note.startBeat + note.lengthBeats;
    std::optional<synth::NoteId> best;
    double bestStart = 0.0;
    for (const auto& other : clip->notes) {
        if (other.id == note.id || other.pitch != note.pitch)
            continue; // glue joins ONE voice: a neighbour at another pitch is a different line
        if (other.startBeat < noteEnd - kBeatEpsilon)
            continue; // starts before the clicked note ends — not a "next" note
        if (!best || other.startBeat < bestStart) {
            best = other.id;
            bestStart = other.startBeat;
        }
    }
    return best;
}

void PianoRollComponent::performGlue(synth::NoteId id) {
    const auto* found = doc_ != nullptr ? doc_->getNote(id) : nullptr;
    if (found == nullptr)
        return;
    const synth::MidiNote clicked = *found;

    const auto candidate = glueCandidateFor(clicked);
    if (!candidate)
        return; // nothing to absorb — a no-op, and no undo step

    const auto* nextNote = doc_->getNote(*candidate);
    if (nextNote == nullptr)
        return;
    // The gap between them is BRIDGED (Cubase's behaviour): the survivor runs from the clicked
    // note's start to the absorbed note's end, which is the only way to glue a staccato pair back
    // into one sustained note. A glue that only closed touching notes would do nothing useful.
    const double newLength = nextNote->startBeat + nextNote->lengthBeats - clicked.startBeat;
    const auto absorbedId = *candidate;

    auto mutate = [this, id, newLength, absorbedId] {
        doc_->resizeNote(id, newLength);
        doc_->removeNote(absorbedId);
    };
    if (undoManager_)
        undoManager_->recordTimelineChange(*doc_, mutate);
    else
        mutate();

    selection_.remove(absorbedId);
    repaint();
}

void PianoRollComponent::performErase(synth::NoteId id) {
    if (doc_ == nullptr || doc_->getNote(id) == nullptr)
        return;
    auto mutate = [this, id] { doc_->removeNote(id); };
    if (undoManager_)
        undoManager_->recordTimelineChange(*doc_, mutate);
    else
        mutate();

    selection_.remove(id);
    clearSplitPreview();
    repaint();
}

void PianoRollComponent::performMuteToggle(synth::NoteId id) {
    const auto* note = doc_ != nullptr ? doc_->getNote(id) : nullptr;
    if (note == nullptr)
        return;
    const bool wanted = !note->muted;
    auto mutate = [this, id, wanted] { doc_->setNoteMuted(id, wanted); };
    if (undoManager_)
        undoManager_->recordTimelineChange(*doc_, mutate);
    else
        mutate();
    repaint();
}

//==============================================================================
// ---- Split-tool hover preview (state-change gated; see the class comment) ----

juce::Rectangle<int> PianoRollComponent::splitPreviewStrip() const {
    const auto* clip = doc_ != nullptr ? doc_->getClip(clipId_) : nullptr;
    const auto* note = doc_ != nullptr ? doc_->getNote(splitPreviewNote_) : nullptr;
    if (clip == nullptr || note == nullptr)
        return {};
    const int x = (int)std::llround(beatToX(clip->startBeat + splitPreviewBeat_));
    const juce::Rectangle<int> strip(x - 2, yForPitch(note->pitch), 5, std::max(1, (int)pixelsPerSemitone_));
    return strip.getIntersection(gridRegion());
}

void PianoRollComponent::updateSplitPreview(juce::Point<int> pos) {
    synth::NoteId note;
    double beat = 0.0;
    bool has = false;

    if (activeTool_ == EditTool::Split && doc_ != nullptr && clipId_.isValid() && pos.y >= canvasTop() &&
        pos.x >= leftGutterWidth()) {
        if (auto hit = hitTestNote(pos)) {
            if (const auto* hovered = doc_->getNote(hit->id)) {
                if (auto cut = splitBeatFor(*hovered, pos.x)) {
                    note = hit->id;
                    beat = *cut;
                    has = true;
                }
            }
        }
    }

    // THE gate: a pointer sliding around inside one note at one snap division changes nothing, so
    // it costs nothing. Only a different note or a different snapped cut repaints, and then only
    // the two one-row strips involved.
    if (has == hasSplitPreview_ && note == splitPreviewNote_ &&
        (!has || std::abs(beat - splitPreviewBeat_) < kBeatEpsilon))
        return;

    const auto oldStrip = hasSplitPreview_ ? splitPreviewStrip() : juce::Rectangle<int>();
    hasSplitPreview_ = has;
    splitPreviewNote_ = note;
    splitPreviewBeat_ = beat;
    const auto newStrip = has ? splitPreviewStrip() : juce::Rectangle<int>();

    const auto strip = oldStrip.isEmpty() ? newStrip : (newStrip.isEmpty() ? oldStrip : oldStrip.getUnion(newStrip));
    if (!strip.isEmpty())
        requestRepaintPreviewStrip(strip);
}

void PianoRollComponent::clearSplitPreview() {
    if (!hasSplitPreview_)
        return;
    const auto strip = splitPreviewStrip();
    hasSplitPreview_ = false;
    splitPreviewNote_ = {};
    splitPreviewBeat_ = 0.0;
    if (!strip.isEmpty())
        requestRepaintPreviewStrip(strip);
}

//==============================================================================
// ---- Tool cursors ----

void PianoRollComponent::rebuildToolCursors() {
    auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());
    for (auto tool : kAllEditTools) {
        std::unique_ptr<juce::Drawable> icon;
        if (lf != nullptr)
            icon = lf->getIcon(iconForTool(tool));
        // makeToolCursor is null-safe by contract (headless builds have no icon assets at all).
        toolCursors_[(size_t)tool] = makeToolCursor(tool, icon.get());
    }
    toolCursorsBuilt_ = true;
}

juce::MouseCursor PianoRollComponent::cursorForActiveTool() {
    if (!toolCursorsBuilt_)
        rebuildToolCursors();
    return toolCursors_[(size_t)activeTool_];
}

void PianoRollComponent::applyToolCursor() {
    showingResizeCursor_ = false;
    setMouseCursor(cursorForActiveTool());
}

void PianoRollComponent::updateHoverCursor(juce::Point<int> pos) {
    if (activeTool_ != EditTool::Select)
        return; // the other five tools act on a click — their cursor never changes on hover

    bool onEdge = false;
    if (doc_ != nullptr && clipId_.isValid() && pos.y >= canvasTop() && pos.x >= leftGutterWidth()) {
        if (auto hit = hitTestNote(pos))
            onEdge = hit->onRightEdge;
    }
    if (onEdge == showingResizeCursor_)
        return; // state-change gate again: no per-move cursor churn

    showingResizeCursor_ = onEdge;
    setMouseCursor(onEdge ? juce::MouseCursor(juce::MouseCursor::LeftRightResizeCursor) : cursorForActiveTool());
}

void PianoRollComponent::lookAndFeelChanged() {
    // The cursors are rasterised from the THEMED icons, so a theme switch invalidates all six.
    toolCursorsBuilt_ = false;
    applyToolCursor();
}

void PianoRollComponent::setRulerBandHeight(int heightPx) {
    const int clamped = std::max(0, heightPx);
    if (clamped == rulerBandHeight_)
        return; // no-op: the owner re-runs its layout constantly, and canvasTop() moving is expensive
    rulerBandHeight_ = clamped;
    resized(); // canvasTop() moved, so the keys column and the grid rect both have to be re-carved
    repaint();
}

int PianoRollComponent::canvasTop() const noexcept { return kToolbarHeight + rulerBandHeight_; }
int PianoRollComponent::getRulerBandHeight() const noexcept { return rulerBandHeight_; }

//==============================================================================
// ---- Simple accessors (moved out of the header — see PianoRollComponent.h for each contract) ----
EditTool PianoRollComponent::getActiveTool() const noexcept { return activeTool_; }

bool PianoRollComponent::isMarqueeActiveForTest() const noexcept { return dragMode_ == DragMode::Marquee; }

bool PianoRollComponent::hasSplitPreviewForTest() const noexcept { return hasSplitPreview_; }
double PianoRollComponent::getSplitPreviewBeatForTest() const noexcept { return splitPreviewBeat_; }
synth::NoteId PianoRollComponent::getSplitPreviewNoteForTest() const noexcept { return splitPreviewNote_; }
double PianoRollComponent::getDrawPreviewLengthForTest() const noexcept {
    return dragMode_ == DragMode::DrawNew ? drawLengthBeats_ : 0.0;
}

double PianoRollComponent::getResizeDeltaForTest() const noexcept {
    return dragMode_ == DragMode::Resize ? previewLengthDelta_ : 0.0;
}
int PianoRollComponent::getResizeNoteCountForTest() const noexcept {
    return dragMode_ == DragMode::Resize ? (int)resizeNotes_.size() : 0;
}
bool PianoRollComponent::isResizeUnquantizedForTest() const noexcept {
    return dragMode_ == DragMode::Resize && resizeUnquantized_;
}

} // namespace synth::ui
