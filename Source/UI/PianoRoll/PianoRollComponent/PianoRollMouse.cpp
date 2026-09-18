// PianoRollComponent — mouse handling: mouseDown/mouseDrag/mouseUp dispatch across drag modes,
// mouse-wheel scroll/zoom, and edge-auto-scroll while a drag holds the pointer near the canvas
// edge. The class itself is declared in PianoRollComponent.h; sibling PianoRoll<Concern>.cpp units
// in this directory hold the rest (construction/geometry, scale assist, painting, edit tools,
// audition, clipboard, zoom).

#include "PianoRollComponent.h"

#include "AppUndoManager.h"
#include "PianoRollInternal.h"
#include "UI/Timeline/EdgeAutoScroll.h"
#include "UI/Timeline/ScrollPolicy.h"
#include <algorithm>
#include <cmath>

namespace synth::ui {

using namespace synth::ui::detail;

//==============================================================================
// ---- Mouse ----

void PianoRollComponent::mouseDown(const juce::MouseEvent& e) {
    grabKeyboardFocus();
    dragMode_ = DragMode::None;
    pendingEmptyClick_ = false;
    // Latched per gesture (see resizeUnquantized_/moveUnquantized_): cleared here so a previous
    // Cmd+drag can never leak its grid bypass into the next, plain, one.
    resizeUnquantized_ = false;
    moveUnquantized_ = false;
    cmdToggleNote_ = {};
    cmdToggleWasSelected_ = false;

    if (doc_ == nullptr || !clipId_.isValid() || doc_->getClip(clipId_) == nullptr)
        return;
    if (!e.mods.isLeftButtonDown())
        return; // no right-click menu in v1

    const auto pos = e.getPosition();
    // The beat/pitch under the pointer RIGHT NOW, captured before any of the branches below run —
    // every Move/DrawNew drag's delta is computed against these, not against a re-derived
    // xToBeat(mouseDownPos_.x)/pitchForY(mouseDownPos_.y), so a mid-drag edge-scroll (which moves
    // rollView_/firstVisiblePitch_) can never retroactively change what the anchor meant. Harmless
    // to compute even for a click that turns out not to start a drag at all (the header buttons,
    // an empty-grid deselect).
    mouseDownBeat_ = xToBeat((double)pos.x);
    mouseDownPitch_ = pitchForY(pos.y);
    lastDragPointer_ = pos;
    if (backButtonBounds_.contains(pos)) {
        requestClose();
        return;
    }
    // Each chip now does exactly ONE thing on a plain click — no modifier variants anywhere in the
    // header. Snap and quantise used to share the single "Q" chip (plain click vs Shift+click), which
    // is precisely the ambiguity the split into separate glyph chips removes. The Snap chip itself
    // was later removed (FRO108) — it duplicated the timeline toolbar's own Snap button on the same
    // shared TimelineViewState::snapEnabled; toggleSnap() and the J key still work unchanged.
    if (quantiseButtonBounds_.contains(pos)) {
        flashQuantiseButton(); // feedback even when the click is a no-op
        performQuantise();
        return;
    }
    if (quantiseLengthButtonBounds_.contains(pos)) {
        flashQuantiseLengthButton(); // mirrors the Quantise chip above: same isQuantiseEnabled() gate
        performQuantiseLength();
        return;
    }
    if (quantisePitchButtonBounds_.contains(pos)) {
        quantisePitchesToActiveScale(); // silently a no-op with no scale chosen, like the chip's dim
        return;
    }
    if (scaleButtonBounds_.contains(pos)) {
        toggleScalePanel();
        return;
    }
    if (scaleFilterButtonBounds_.contains(pos)) {
        toggleScaleFilter();
        return;
    }
    if (pos.y < canvasTop())
        return; // rest of the header strip: inert
    if (pos.x < leftGutterWidth()) {
        // KEYS COLUMN = a virtual keyboard. Pressing a key auditions that pitch through the SAME
        // onAuditionNote path a note click uses, so it reaches the same destination modules the
        // track plays through, with the same no-stuck-note guarantees. The scale panel (also left of
        // the gutter while open) is a real child component and never reaches this branch — but
        // isKeysColumnPoint checks the column's own rect anyway rather than trusting that.
        beginKeysColumnPress(pos);
        return;
    }

    // Everything below this line is the SELECT tool's gesture table. The other five tools act on
    // the click alone and start no drag at all (see setActiveTool), so they never reach it — no
    // modifier combination can turn an Erase click into a move.
    if (activeTool_ != EditTool::Select) {
        handleToolMouseDown(pos);
        return;
    }

    auto hit = hitTestNote(pos);

    // AUDITION — before any of the branches below, so every note-hit gesture sounds the note
    // identically (select, move, resize, velocity scrub, even a Shift-click that deselects it):
    // "clicking a note plays it" must not depend on which modifier happened to be down. Confined to
    // the Select tool — an Erase/Mute/Split/Glue click is not a request to hear anything, and those
    // tools returned above. The velocity is the note's OWN, so a quiet note previews quiet.
    if (hit) {
        if (const auto* note = doc_->getNote(hit->id))
            startAudition(note->pitch, note->velocity);
    }

    // CMD now means ONE thing on a note, whichever part of it you grab: "do this without the grid".
    // Right edge -> unsnapped resize, body -> unsnapped move. That consistency is why velocity scrub
    // moved off Cmd and onto Option below; a single modifier meaning "smooth" on one half of a note
    // and "change the volume" on the other half was the thing worth fixing.
    if (hit && e.mods.isCommandDown() && !e.mods.isShiftDown()) {
        if (hit->onRightEdge) {
            if (!selection_.contains(hit->id))
                selection_.setSelection({hit->id});
            resizeUnquantized_ = true;
            beginMoveOrResize(*hit, pos);
            repaint();
            return;
        }

        // Note BODY. Cmd+CLICK is an additive-select toggle and Cmd+DRAG is an unsnapped move, and at
        // mouse-down those are indistinguishable — so the note is ADDED now (the move needs it in the
        // selection) and mouse-up completes the toggle if nothing actually moved. See cmdToggleNote_.
        cmdToggleNote_ = hit->id;
        cmdToggleWasSelected_ = selection_.contains(hit->id);
        selection_.add(hit->id);
        moveUnquantized_ = true;
        beginMoveOrResize(*hit, pos);
        repaint();
        return;
    }

    if (hit && e.mods.isAltDown() && !e.mods.isShiftDown()) {
        // Velocity scrub, moved here from Cmd (see above). Option is free for a mouse drag on this
        // surface: the only other Option bindings the roll owns are KEY chords (Alt+arrows navigate
        // notes, Option+S toggles the row filter), and a modifier can mean one thing for the keyboard
        // and another for the mouse without either being ambiguous. Still ADDITIVE, never a toggle —
        // the drag scrubs the whole selection's velocity, so yanking the grabbed note out of it
        // mid-gesture is never what was wanted.
        selection_.add(hit->id);
        beginVelocityScrub(pos);
        repaint();
        return;
    }

    if (hit) {
        if (e.mods.isShiftDown()) {
            selection_.toggle(hit->id);
            repaint();
            return; // additive click never begins a drag
        }
        if (!selection_.contains(hit->id))
            selection_.setSelection({hit->id});
        beginMoveOrResize(*hit, pos);
        repaint();
        return;
    }

    // Empty grid. EVERY drag from here marquees — multi-select is the plain gesture in the roll,
    // unlike the graph editor where plain drag has to stay free for panning. Shift / Cmd / Ctrl only
    // change WHAT the marquee does with the existing selection: plain REPLACES it, a modifier keeps
    // it and ADDS to it.
    //
    // A press that never becomes a drag is still the old plain click-through that DESELECTS, and at
    // mouse-down time the two are indistinguishable — hence the deferral (pendingEmptyClick_,
    // promoted to a marquee by mouseDrag, resolved to a deselect by mouseUp). Creating a note is
    // the double-click (mouseDoubleClick) — a single click never draws.
    mouseDownPos_ = pos;
    if (e.mods.isShiftDown() || e.mods.isCommandDown() || e.mods.isCtrlDown()) {
        beginMarquee(pos, /*additive*/ true);
        return;
    }
    pendingEmptyClick_ = true;
}

void PianoRollComponent::mouseDrag(const juce::MouseEvent& e) {
    // A keys-column press is its own gesture: it starts no drag mode, so it has to be handled before
    // the note-gesture machinery below (which would read dragMode_ == None and return anyway).
    if (keysColumnPressing_) {
        updateKeysColumnPress(e.getPosition());
        return;
    }

    if (pendingEmptyClick_) {
        // The press mouseDown could not classify has now moved: it was a marquee, not a deselect.
        // Plain drag REPLACES the selection (the modifier variants armed themselves additively at
        // mouse-down and never reach here), and it anchors on the PRESS point, not on this event's
        // position — a marquee that started where the finger went down is the only one that can
        // enclose what the user swept over.
        pendingEmptyClick_ = false;
        beginMarquee(mouseDownPos_, /*additive*/ false);
    }

    if (dragMode_ == DragMode::Marquee) {
        lastDragPointer_ = e.getPosition();
        updateDragPreviewFromLastPointer();
        updateAutoScrollArming();
        return;
    }
    if (dragMode_ == DragMode::None || doc_ == nullptr || !clipId_.isValid())
        return;

    if (doc_->getClip(clipId_) == nullptr) {
        dragMode_ = DragMode::None;
        return;
    }

    lastDragPointer_ = e.getPosition();
    updateDragPreviewFromLastPointer();
    updateAutoScrollArming();
}

void PianoRollComponent::updateDragPreviewFromLastPointer() {
    // Marquee has its own preview writer (updateMarquee, which recomputes the swept selection from
    // collectNoteRects() — already beat/row-aware, so it needs no anchor of its own) and no doc/
    // clip requirement of its own; every other mode below is document-editing and needs both.
    if (dragMode_ == DragMode::Marquee) {
        updateMarquee(lastDragPointer_);
        return;
    }
    if (dragMode_ == DragMode::None || doc_ == nullptr || !clipId_.isValid())
        return;
    const auto* clip = doc_->getClip(clipId_);
    if (clip == nullptr) {
        dragMode_ = DragMode::None;
        return;
    }

    const auto pos = lastDragPointer_;

    if (dragMode_ == DragMode::DrawNew) {
        // The pencil's anchor never moves — only the length follows the pointer, snapped exactly
        // the way a right-edge resize is, so drawing and then resizing a note feel identical.
        const double grid = currentGridBeats();
        const double minLen = grid > 0.0 ? grid : kMinNoteLengthBeats;
        const double snappedEnd = snappedBeatAt(xToBeat((double)pos.x) - clip->startBeat);
        double length = std::max(snappedEnd - drawStartBeat_, minLen);
        length = std::min(length, std::max(0.0, clip->lengthBeats - drawStartBeat_));
        drawLengthBeats_ = length;
        repaint();
        return;
    }

    if (dragMode_ == DragMode::Move) {
        double anchorOriginalStart = 0.0;
        for (const auto& origin : dragNotes_)
            if (origin.id == activeNote_)
                anchorOriginalStart = origin.startBeat;

        // BEAT-anchored, not pixel-anchored: the delta is xToBeat(current x) - mouseDownBeat_, so
        // a view scroll that happens mid-drag (an edge-scroll tick, or any other scroll) is baked
        // into xToBeat's OWN firstVisibleBeat term rather than silently invalidating a delta
        // computed against the scroll position that was current at mouseDown. See
        // mouseDownBeat_'s comment.
        const double deltaBeatsRaw = xToBeat((double)pos.x) - mouseDownBeat_;
        // Cmd bypasses the grid entirely (the note follows the pointer continuously), the same way it
        // does for a right-edge resize. Latched at mouse-down (moveUnquantized_), never re-read from
        // the live modifiers — a gesture must not change meaning half way through because Cmd was
        // released.
        const double targetAnchorStart =
            moveUnquantized_ ? anchorOriginalStart + deltaBeatsRaw : snappedBeatAt(anchorOriginalStart + deltaBeatsRaw);
        double delta = targetAnchorStart - anchorOriginalStart;

        // Clamp the shared delta so the WHOLE group stays inside [0, clipLength) — the same
        // clamp-the-group-together reasoning TimelineClipLaneArea::mouseDrag's Move branch uses
        // for its "no clip's start goes negative" clamp, extended with an upper bound because
        // notes (unlike clips) are bounded by their clip's window.
        double minOriginal = 0.0, maxEnd = 0.0;
        bool first = true;
        for (const auto& origin : dragNotes_) {
            if (first || origin.startBeat < minOriginal)
                minOriginal = origin.startBeat;
            const double end = origin.startBeat + origin.lengthBeats;
            if (first || end > maxEnd)
                maxEnd = end;
            first = false;
        }
        delta = std::max(delta, -minOriginal);
        delta = std::min(delta, clip->lengthBeats - maxEnd);
        previewDeltaBeats_ = delta;

        // Pitch delta is a ROW delta in visiblePitches_ space, NOT a semitone delta: with
        // pitch-visibility collapsing out-of-scale rows, a raw pixel/pixelsPerSemitone conversion
        // added straight to a note's pitch could land it on a pitch that is not itself visible.
        // Both ends of the gesture go through pitchForY (already row-aware — see the class
        // comment) rather than a plain pixel division, so the drag snaps to visible rows exactly
        // the way clicking one does. In the common unfiltered case this is numerically identical
        // to the old semitone math, because row index == pitch there.
        const long long currentRow = (long long)nearestVisibleRowIndex(pitchForY(pos.y));
        const long long anchorRow = (long long)nearestVisibleRowIndex(mouseDownPitch_);
        long long rowDeltaRaw = currentRow - anchorRow;

        long long minRow = 0, maxRow = 0;
        bool firstRow = true;
        for (const auto& origin : dragNotes_) {
            const long long row = (long long)nearestVisibleRowIndex(origin.pitch);
            if (firstRow || row < minRow)
                minRow = row;
            if (firstRow || row > maxRow)
                maxRow = row;
            firstRow = false;
        }
        const long long totalRows = (long long)visiblePitches_.size();
        if (totalRows > 0)
            rowDeltaRaw = std::clamp(rowDeltaRaw, -minRow, totalRows - 1 - maxRow);
        else
            rowDeltaRaw = 0;
        previewDeltaPitch_ = (int)rowDeltaRaw;

        // AUDITION retrigger: the preview pitch of the GRABBED note (never the whole group — one
        // sounding note per gesture, the same way a keyboard plays the key you are on). Resolved
        // through the SAME rowShiftedPitch the drawn preview uses, and gated on the pitch actually
        // changing by retriggerAudition, so dragging horizontally inside one row costs nothing.
        for (const auto& origin : dragNotes_) {
            if (origin.id != activeNote_)
                continue;
            retriggerAudition(rowShiftedPitch(origin.pitch, previewDeltaPitch_));
            break;
        }
    } else if (dragMode_ == DragMode::Resize) {
        const auto* note = doc_->getNote(activeNote_);
        if (note == nullptr)
            return;
        const double grid = currentGridBeats();
        const double rawEnd = xToBeat((double)pos.x) - clip->startBeat;
        // Cmd bypasses the grid ENTIRELY (11.2): the raw beat under the pointer, and the floor drops
        // to the editor's absolute minimum, so the drag is continuous rather than stepping. Latched
        // at mouse-down (resizeUnquantized_), never re-read from the live modifiers.
        const double targetEnd = resizeUnquantized_ ? rawEnd : snappedBeatAt(rawEnd);
        const double minLen = (resizeUnquantized_ || grid <= 0.0) ? kMinNoteLengthBeats : grid;
        // NO upper clamp to the clip's length (11.1): a note may be dragged out past the clip's end,
        // and the mouse-up asks whether to grow the clip to fit (promptExtendClipToFitNotes). The
        // old clamp here is what used to make that gesture impossible.
        previewLength_ = std::max(targetEnd - note->startBeat, minLen);
        previewLengthDelta_ = previewLength_ - resizeOriginalLength_;
    } else if (dragMode_ == DragMode::VelocityScrub) {
        // ~1 per px, up = louder (screen y decreases as pitch/velocity "increases" — matching
        // yForPitch's convention). Clamped per-note (independently) in effectiveGeometryFor. Pixel
        // based and stays that way — velocity is not a spatial quantity a beat/row anchor helps.
        previewDeltaVelocity_ = mouseDownPos_.y - pos.y;
    }

    repaint();
}

void PianoRollComponent::updateAutoScrollArming() {
    const bool dragging = dragMode_ == DragMode::Move || dragMode_ == DragMode::Resize ||
                          dragMode_ == DragMode::Marquee || dragMode_ == DragMode::DrawNew;
    const auto grid = gridRegion();
    // maxPerTick=1.0 on both axes here only probes zero-vs-nonzero — the REAL, axis-specific
    // magnitude is read again inside autoScrollTick().
    const bool insideEdgeZone =
        dragging &&
        (edgeScrollVelocity(lastDragPointer_.x, grid.getX(), grid.getRight(), synth::ui::kEdgeZonePx, 1.0) != 0.0 ||
         edgeScrollVelocity(lastDragPointer_.y, grid.getY(), grid.getBottom(), synth::ui::kEdgeZonePx, 1.0) != 0.0);
    if (insideEdgeZone && !autoScrollTimer_.isTimerRunning())
        autoScrollTimer_.startTimer(1000 / synth::ui::kEdgeScrollHz);
    else if (!insideEdgeZone && autoScrollTimer_.isTimerRunning())
        autoScrollTimer_.stopTimer();
}

// THE edge-auto-scroll timer's seam, mirroring TimelineClipLaneArea::autoScrollTick() exactly.
void PianoRollComponent::autoScrollTick() {
    // The drag can have ended (mouseUp) or moved out of the zone since the last arming check
    // without another tick having run updateAutoScrollArming() itself — re-check both here rather
    // than trusting the timer's own "it was armed a tick ago" state.
    const bool dragging = dragMode_ == DragMode::Move || dragMode_ == DragMode::Resize ||
                          dragMode_ == DragMode::Marquee || dragMode_ == DragMode::DrawNew;
    if (!dragging) {
        autoScrollTimer_.stopTimer();
        return;
    }

    const auto grid = gridRegion();
    const double hVelocityPxPerTick = edgeScrollVelocity(lastDragPointer_.x, grid.getX(), grid.getRight(),
                                                         synth::ui::kEdgeZonePx, kEdgeAutoScrollMaxPxPerTick);
    const double vVelocityRowsPerTick = edgeScrollVelocity(lastDragPointer_.y, grid.getY(), grid.getBottom(),
                                                           synth::ui::kEdgeZonePx, kEdgeAutoScrollMaxRowsPerTick);
    if (hVelocityPxPerTick == 0.0 && vVelocityRowsPerTick == 0.0) {
        autoScrollTimer_.stopTimer(); // the pointer drifted back into the dead middle band
        return;
    }

    bool horizontalMoved = false;
    if (hVelocityPxPerTick != 0.0 && rollView_.pixelsPerBeat > 0.0) {
        rollView_.scrollBeats(hVelocityPxPerTick / rollView_.pixelsPerBeat);
        horizontalMoved = true;
    }
    if (vVelocityRowsPerTick != 0.0 && !visiblePitches_.empty()) {
        // Sign is INVERTED relative to the horizontal case: edgeScrollVelocity returns negative
        // near the LOW edge (here, the TOP of the grid) and positive near the HIGH edge (the
        // bottom). Near the top the user wants to reveal what is ABOVE — higher pitches, since
        // pitch increases upward — which means topRowPosition_ (the row at the TOP) must INCREASE,
        // the opposite sign from the raw velocity. No std::llround here any more: the old
        // per-tick rounding meant any penetration under half the edge zone rounded to a ZERO row
        // step — a "dead" outer half of the zone that never scrolled at all, however long the
        // pointer sat there. Adding the raw fractional velocity straight into topRowPosition_
        // fixes that (every tick, at any penetration depth, makes SOME progress) and is what makes
        // a fast run of ticks glide smoothly through rows instead of hopping by whole ones.
        setTopRowPosition(topRowPosition_ - vVelocityRowsPerTick);
    }

    // The pointer hasn't moved (no MouseEvent fired this tick) — the view did, so whichever
    // gesture is in flight has to be re-derived against the NEW rollView_/firstVisiblePitch_ from
    // the same last-known pointer position. Every branch of updateDragPreviewFromLastPointer
    // already repaints on a real change, so nothing further is needed here for that half of it —
    // only the scroll itself (which touched no note geometry) needs its own repaint below.
    updateDragPreviewFromLastPointer();
    if (horizontalMoved && onHorizontalViewChanged)
        onHorizontalViewChanged();
    repaint();
}

void PianoRollComponent::mouseUp(const juce::MouseEvent&) {
    // The release always ends whatever drag was in flight, so the edge-scroll timer never
    // outlives it — stopped unconditionally rather than only from the branches below, since a
    // marquee/pendingEmptyClick release reaches this point too and the timer must not care which.
    autoScrollTimer_.stopTimer();
    // Same "unconditional, before any branch returns" reasoning, and for a sharper reason: EVERY
    // early return below is a path a note-on must not survive. A no-op when nothing is sounding.
    stopAudition();
    endKeysColumnPress(); // releases the virtual key's pressed paint; the note-off came from above

    if (dragMode_ == DragMode::Marquee) {
        endMarquee();
        dragMode_ = DragMode::None;
        pendingEmptyClick_ = false;
        repaint();
        return;
    }

    if (pendingEmptyClick_) {
        // Press+release on empty grid without ever crossing the drag threshold: the deselect the
        // press deferred. Selection is not document state, so this writes nothing and pushes no
        // undo step — and an already-empty selection repaints nothing at all.
        pendingEmptyClick_ = false;
        if (!selection_.isEmpty()) {
            selection_.clear();
            repaint();
        }
        return;
    }

    if (doc_ == nullptr || !clipId_.isValid()) {
        dragMode_ = DragMode::None;
        return;
    }

    if (dragMode_ == DragMode::DrawNew) {
        // A plain click and a drag commit through the SAME path: the click simply never changed
        // the one-division length the press armed. One addNote, one undo step, note selected.
        const double start = drawStartBeat_;
        const double length = drawLengthBeats_;
        const int pitch = drawPitch_;
        dragMode_ = DragMode::None;
        drawLengthBeats_ = 0.0;
        commitNewNote(start, length, pitch);
        return;
    }

    // A Cmd press on a note BODY that never actually moved anything was a Cmd+CLICK, not a
    // Cmd+drag: finish the additive-select TOGGLE the press could only half-commit. A note that was
    // already selected before the press comes back out; one that wasn't simply stays added. Tested
    // before the Move commit below and short-circuits it, because "nothing moved" is exactly the
    // condition that commit already declines to act on.
    if (cmdToggleNote_.isValid() && dragMode_ == DragMode::Move && std::abs(previewDeltaBeats_) <= 1e-9 &&
        previewDeltaPitch_ == 0) {
        if (cmdToggleWasSelected_)
            selection_.remove(cmdToggleNote_);
        cmdToggleNote_ = {};
        cmdToggleWasSelected_ = false;
        dragMode_ = DragMode::None;
        dragNotes_.clear();
        moveUnquantized_ = false;
        repaint();
        return;
    }

    if (dragMode_ == DragMode::Move && (std::abs(previewDeltaBeats_) > 1e-9 || previewDeltaPitch_ != 0)) {
        const auto notes = dragNotes_;
        const double delta = previewDeltaBeats_;
        // A ROW delta (see previewDeltaPitch_) — resolved through the SAME rowShiftedPitch the
        // preview used, so the committed pitch can never disagree with what was drawn.
        const long long rowDelta = previewDeltaPitch_;
        auto mutate = [this, notes, delta, rowDelta] {
            for (const auto& origin : notes)
                doc_->moveNote(origin.id, origin.startBeat + delta, rowShiftedPitch(origin.pitch, rowDelta));
        };
        if (undoManager_)
            undoManager_->recordTimelineChange(*doc_, mutate);
        else
            mutate();
    } else if (dragMode_ == DragMode::Resize && std::abs(previewLength_ - resizeOriginalLength_) > 1e-9) {
        // ONE undo step for however many notes the gesture snapshotted (11.1). The per-note lengths
        // are resolved through resizePreviewLengthFor — the SAME function the preview painted with,
        // so what is committed can never disagree with what was drawn.
        std::vector<std::pair<synth::NoteId, double>> targets;
        targets.reserve(resizeNotes_.size());
        for (const auto& origin : resizeNotes_)
            targets.emplace_back(origin.id, resizePreviewLengthFor(origin));

        auto mutate = [this, targets] {
            for (const auto& target : targets)
                doc_->resizeNote(target.first, target.second);
        };
        if (undoManager_)
            undoManager_->recordTimelineChange(*doc_, mutate);
        else
            mutate();

        // The resize itself has no clip-length clamp any more, so ask about the overrun AFTER
        // committing rather than silently trimming it: the notes are already the length the user
        // dragged, and growing the clip is a SEPARATE answer to a separate question (its own undo
        // step — see extendClipTo). Read back from the doc, not from `targets`, so a length
        // TimelineDoc itself rejected or adjusted can't make us prompt for a clip nobody needs.
        std::vector<synth::NoteId> resizedIds;
        resizedIds.reserve(targets.size());
        for (const auto& target : targets)
            resizedIds.push_back(target.first);
        const double maxEnd = maxNoteEndAmong(resizedIds);
        const auto* clip = doc_->getClip(clipId_);
        if (clip != nullptr && maxEnd > clip->lengthBeats + kBeatEpsilon)
            // clipId_ read HERE, at prompt time, and carried through the answer — the clip whose
            // notes overran, not whatever is open when the alert is finally answered.
            promptExtendClipToFitNotes(clipId_, maxEnd);
    } else if (dragMode_ == DragMode::VelocityScrub && previewDeltaVelocity_ != 0) {
        const auto notes = dragNotes_;
        const int delta = previewDeltaVelocity_;
        auto mutate = [this, notes, delta] {
            for (const auto& origin : notes)
                doc_->setNoteVelocity(origin.id, juce::jlimit(1, 127, origin.velocity + delta));
        };
        if (undoManager_)
            undoManager_->recordTimelineChange(*doc_, mutate);
        else
            mutate();
    }

    dragMode_ = DragMode::None;
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
    repaint();
}

void PianoRollComponent::mouseDoubleClick(const juce::MouseEvent& e) {
    if (doc_ == nullptr || !clipId_.isValid())
        return;
    // Create-and-delete-by-double-click belongs to the SELECT tool. Under any other tool both
    // clicks have already been handled as that tool's own single-click action, and adding a note
    // on top of (say) a second Erase click would be a gesture nobody asked for.
    if (activeTool_ != EditTool::Select)
        return;
    const auto pos = e.getPosition();
    if (pos.y < canvasTop() || pos.x < leftGutterWidth())
        return;

    // JUCE dispatches this AFTER the second mouseDown/mouseUp pair, so whatever those did (select a
    // note, deselect on empty grid) has already happened — this is the last word either way.
    dragMode_ = DragMode::None;
    pendingEmptyClick_ = false;

    if (auto hit = hitTestNote(pos)) {
        const auto id = hit->id;
        auto mutate = [this, id] { doc_->removeNote(id); };
        if (undoManager_)
            undoManager_->recordTimelineChange(*doc_, mutate);
        else
            mutate();
        selection_.remove(id);
        repaint();
        return;
    }

    createNoteAt(pos);
}

// BOTH the Split-tool cut preview and the Select-tool resize-zone cursor are gated on a state change
// (the snapped cut beat / the hovered note, and the "is the pointer in a resize zone" boolean), so a
// mouse moving inside one note at one snap division costs zero repaints and zero cursor churn — see
// the repaint invariant in CLAUDE.md.
void PianoRollComponent::mouseMove(const juce::MouseEvent& e) {
    const auto pos = e.getPosition();
    updateSplitPreview(pos);
    updateHoverCursor(pos);
    updateHeaderButtonHover(pos);
}

void PianoRollComponent::mouseEnter(const juce::MouseEvent& e) {
    // The cursor is set on ENTER (and on a tool change), never per move — six rasterised icons is
    // not per-frame work.
    applyToolCursor();
    updateSplitPreview(e.getPosition());
    updateHoverCursor(e.getPosition());
    updateHeaderButtonHover(e.getPosition());
}

void PianoRollComponent::mouseExit(const juce::MouseEvent&) {
    clearSplitPreview();
    updateHeaderButtonHover({-1, -1}); // off the component entirely -- clears whichever chip was lit
}

// EVERY branch below reads its amount through synth::ui::ScrollPolicy (ScrollPolicy.h) rather than a
// raw delta member, for the two reasons spelled out there: macOS folds Shift+wheel into `deltaX`, so
// the modifier-decided branches (both zooms) must take the DOMINANT axis or go silently dead under
// Shift; and the plain-scroll branches route their sign through scrollAmount() so "natural" here
// means exactly what it means in a juce::Viewport.
//
// The two zoom branches are a DIFFERENT preference from the scroll branches: direction there comes
// from synth::ui::wheelGestureIsUpward (the PHYSICAL gesture, recovered from isReversed XOR the
// delta's sign — see ScrollPolicy.h), not from the delta's raw sign, so "wheel up zooms in" is the
// same finger motion regardless of the OS's natural-scrolling setting. zoomScrollInverted_
// (setZoomScrollInverted) flips that outcome; it is independent of scrollInverted_, which only ever
// governs the plain-scroll branches below.
void PianoRollComponent::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) {
    const bool command = e.mods.isCommandDown();
    const bool shift = e.mods.isShiftDown();
    const auto pos = e.getPosition();

    if (command && shift) {
        // Vertical zoom. Direction is the PHYSICAL gesture (up zooms in by default — see
        // wheelZoomFactor below), not the raw delta sign, so it reads the same whether or not the
        // OS has natural scrolling on. The amount comes from dominantWheelDelta, NOT from
        // wheel.deltaY: macOS folds a Shift-held wheel gesture into deltaX, so reading deltaY here
        // meant this branch received exactly 0.0 and the vertical zoom was dead on the platform it
        // was written on.
        zoomVerticalAroundY(wheelZoomFactor(wheel), (double)pos.y);
        return;
    }

    if (command) {
        // Horizontal zoom around the beat under the cursor, through the roll's OWN mapping — the
        // shared TimelineViewState must not move (the lanes behind us keep their own zoom). Same
        // exponential factor the panel's ruler zoom uses, so the two feel identical. Same dominant-
        // axis read and the same up-zooms-in direction convention as the vertical branch above
        // (wheelZoomFactor) — a modifier-decided branch must never depend on which axis the OS
        // parked the gesture on, and the two zoom branches must never disagree about which way is
        // "in".
        zoomHorizontalAroundX(wheelZoomFactor(wheel), std::max(0.0, (double)pos.x - (double)leftGutterWidth()));
        return;
    }

    // Shift+wheel is horizontal scroll; so is a trackpad's own horizontal delta.
    const bool horizontal = shift || std::abs(wheel.deltaX) > std::abs(wheel.deltaY);
    if (horizontal) {
        // Which axis the gesture ARRIVED on, not "the dominant delta": a Shift+wheel the OS left on
        // deltaY and a trackpad's own sideways deltaX are both horizontal scrolls, and either one is
        // the amount to move by. Spelled identically to TimelinePanelComponent::mouseWheelMove's
        // horizontal branch on purpose — the roll and the lanes must answer the same gesture the
        // same way, and this is the one line where they could silently drift.
        const float delta = std::abs(wheel.deltaX) > std::abs(wheel.deltaY) ? wheel.deltaX : wheel.deltaY;
        // scrollAmount is in SCREEN orientation: +x means the view moves RIGHT. Time runs left to
        // right here, so "the view moves right" is literally a larger firstVisibleBeat — this axis
        // needs no mapping of its own.
        const double amountPx = (double)scrollAmount(delta, scrollInverted_) * kScrollPixelsPerWheelUnit;
        if (amountPx != 0.0) {
            rollView_.scrollBeats(amountPx / rollView_.pixelsPerBeat);
            repaint();
            if (onHorizontalViewChanged)
                onHorizontalViewChanged();
        }
        return;
    }

    if (wheel.deltaY != 0.0f) {
        // The pitch axis DOES need a mapping, and ScrollPolicy.h asks for it to be spelled out
        // here: scrollAmount is screen-oriented (+y = the view moves DOWN), while
        // firstVisiblePitch_ is the pitch of the TOP row and pitch grows UPWARD. A view moving down
        // therefore DECREASES it — hence the negation. Net effect at the default (natural): a
        // gesture that a juce::Viewport would answer by scrolling towards the top of its content
        // shows higher pitches here, which is the same thing.
        const double amountY = (double)scrollAmount(dominantWheelDelta(wheel), scrollInverted_);
        // topRowPosition_ moves CONTINUOUSLY, in fractional ROWS — no truncation, no remainder.
        // Before this, a trackpad's small deltaY (often 0.01-0.1) scaled by
        // kPitchScrollSemitonesPerWheelUnit rounded to ZERO whole rows on almost every individual
        // event, and even the accumulator that used to live here (pitchScrollRemainder_) only
        // rescued those LOST deltas — the visible position still didn't move at all until enough of
        // them summed to a whole row, which is what read as chunky next to the horizontal axis.
        // Now the position itself stays fractional, so yForPitch moves a proportionally small
        // number of PIXELS on every event, exactly like rollView_.scrollBeats above (no rounding at
        // all): still walked in ROW units, not raw semitone, for the same reason
        // PianoRollComponent.cpp's coordinate-system contract gives — "scroll one wheel-unit's worth
        // of rows" must mean the same number of ROWS whatever pitch-visibility's collapsed semitone
        // spacing happens to be.
        const double deltaRows = -amountY * kPitchScrollSemitonesPerWheelUnit;
        // Gated on whether the CLAMPED result actually moved, not on the delta being non-zero: a
        // wheel that keeps pushing past the top/bottom row must cost zero repaints.
        if (deltaRows != 0.0 && setTopRowPosition(topRowPosition_ + deltaRows))
            repaint();
    }
}

//==============================================================================
// ---- Simple accessors (moved out of the header — see PianoRollComponent.h for each contract) ----
void PianoRollComponent::tickAutoScrollForTest() { autoScrollTick(); }
bool PianoRollComponent::isAutoScrollTimerRunningForTest() const noexcept { return autoScrollTimer_.isTimerRunning(); }

void PianoRollComponent::setFollowPlayhead(bool follow) noexcept { followPlayhead_ = follow; }
bool PianoRollComponent::isFollowPlayhead() const noexcept { return followPlayhead_; }

} // namespace synth::ui
