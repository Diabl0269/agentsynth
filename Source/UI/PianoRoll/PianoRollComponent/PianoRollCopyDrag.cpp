// PianoRollComponent — the Option+drag note copy: the live copy<->move toggle, the ghost painting,
// the one-undo-step commit, Esc cancel, and the short settle/return tween of the ghost. The class
// itself is declared in PianoRollComponent.h; the drag machinery it rides on (beginMoveOrResize,
// dragNotes_, the preview deltas, snapping, edge auto-scroll) lives in PianoRollMouse.cpp and
// PianoRollEditTools.cpp and is shared with a plain move.

#include "PianoRollComponent.h"

#include "PianoRollInternal.h"
#include "UI/Layout/DragCursor.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <algorithm>
#include <cmath>

namespace synth::ui {

namespace {
// The house motion budget for a ghost dropping into place or flying back to its origin.
constexpr double kGhostSettleMs = 140.0;
// The ghost is a raised surface: translucent body, so the grid and the untouched original show
// through it.
constexpr float kGhostOpacity = 0.85f;
} // namespace

// A copy drag is a Move drag whose `copyDrag_` flag is set. The flag is re-read from the live Option
// state (mouseDrag's event modifiers, and modifierKeysChanged for a key press with a still pointer)
// rather than latched at mouse-down, so Option can be pressed or released mid-drag without
// restarting the gesture. Only meaningful while dragMode_ == Move; every gesture end clears it.
void PianoRollComponent::setCopyDrag(bool copy) {
    if (copy == copyDrag_)
        return;
    copyDrag_ = copy;
    if (copy) {
        showDragCursor(*this, true);
        dragCursorShown_ = true;
    } else {
        updateMoveCursor();
    }
    repaint();
}

// The cursor follows the move gesture: copy while an Option copy is active, grab once a plain move
// has actually shifted the notes, and the tool cursor again as soon as neither holds (release, Esc,
// or Option released before the notes moved). The tool cursor stays in charge until then.
void PianoRollComponent::updateMoveCursor() {
    const bool moving =
        dragMode_ == DragMode::Move && (copyDrag_ || std::abs(previewDeltaBeats_) > 1e-9 || previewDeltaPitch_ != 0);
    if (moving) {
        showDragCursor(*this, copyDrag_);
        dragCursorShown_ = true;
    } else if (dragCursorShown_) {
        dragCursorShown_ = false;
        applyToolCursor();
    }
}

bool PianoRollComponent::isCopyDragForTest() const noexcept { return dragMode_ == DragMode::Move && copyDrag_; }

void PianoRollComponent::modifierKeysChanged(const juce::ModifierKeys& mods) {
    if (dragMode_ == DragMode::Move)
        setCopyDrag(mods.isAltDown());
    juce::Component::modifierKeysChanged(mods); // keep the default bubbling to the parent
}

// The commit goes through the clipboard's own build/commit pair: the ghosts' final geometry becomes
// ClipboardNote entries anchored at the earliest dragged note plus the preview delta, so the
// clip-window clipping and the single recordTimelineChange + "select what was just placed"
// behaviour are exactly a paste's. A zero net delta commits nothing (no undo step).
void PianoRollComponent::commitCopyDrag() {
    if (dragNotes_.empty() || (std::abs(previewDeltaBeats_) <= 1e-9 && previewDeltaPitch_ == 0))
        return;

    double earliest = dragNotes_.front().startBeat;
    for (const auto& origin : dragNotes_)
        earliest = std::min(earliest, origin.startBeat);

    std::vector<ClipboardNote> entries;
    entries.reserve(dragNotes_.size());
    for (const auto& origin : dragNotes_) {
        const auto* source = doc_->getNote(origin.id);
        entries.push_back({origin.startBeat - earliest, origin.lengthBeats,
                           rowShiftedPitch(origin.pitch, previewDeltaPitch_), origin.velocity,
                           source != nullptr ? source->channel : 1, source != nullptr && source->muted});
    }

    std::vector<synth::MidiNote> notes;
    if (buildPastedNotes(entries, earliest + previewDeltaBeats_, notes) && commitPastedNotes(notes))
        startGhostSettle(true);
}

// Esc during a Move/copy drag: the gesture is dropped without touching the document, so there is no
// undo step. Selection is view state and stays as the press left it. The mouse-up that follows finds
// dragMode_ == None and only runs the common reset.
bool PianoRollComponent::cancelNoteDrag() {
    if (dragMode_ != DragMode::Move)
        return false;

    if (copyDrag_)
        startGhostSettle(false);
    dragMode_ = DragMode::None;
    dragNotes_.clear();
    previewDeltaBeats_ = 0.0;
    previewDeltaPitch_ = 0;
    moveUnquantized_ = false;
    cmdToggleNote_ = {};
    cmdToggleWasSelected_ = false;
    setCopyDrag(false);
    updateMoveCursor();
    autoScrollTimer_.stopTimer();
    stopAudition();
    repaint();
    return true;
}

// Snapshots the ghost (the drag state is about to be cleared) and tweens it for 140 ms on the
// roll's existing VBlank updater: a drop eases out and fades the raised look into the real copies
// underneath; a cancel eases in while the ghost returns to its origin and fades. Headless / off-
// screen there is no VBlank, so it is skipped and the result is immediately final.
void PianoRollComponent::startGhostSettle(bool dropped) {
    if (dragNotes_.empty() || !isShowing())
        return;
    if (!dropped && std::abs(previewDeltaBeats_) <= 1e-9 && previewDeltaPitch_ == 0)
        return; // the ghost never left its origin
    ghostSettle_ = pianoroll::GhostSettle{dragNotes_, previewDeltaBeats_, previewDeltaPitch_, dropped, 0.0f};
    if (!scalePanelVblankUpdater_.has_value())
        scalePanelVblankUpdater_.emplace(this);
    ghostAnim_.start(
        *scalePanelVblankUpdater_, kGhostSettleMs, dropped ? synth::ui::easeOutCubic : synth::ui::easeInCubic,
        [this](float t) {
            if (ghostSettle_.has_value())
                ghostSettle_->t = t;
            repaint();
        },
        [this] { clearGhostSettle(); });
}

void PianoRollComponent::clearGhostSettle() {
    if (scalePanelVblankUpdater_.has_value())
        ghostAnim_.stop(*scalePanelVblankUpdater_);
    if (ghostSettle_.has_value()) {
        ghostSettle_.reset();
        repaint();
    }
}

// The ghost is a raised surface drawn over the notes at the PREVIEW geometry: ~0.85 opacity fill in
// the note's own colour, a 1 px accent border and a soft two-layer shadow (juce::DropShadow's
// per-paint gaussian is avoided on purpose, see AppLookAndFeelCanvasTreatments.cpp). During a
// settle the same surface is drawn from the snapshot, fading out (`fade` 1 -> 0) and, for a
// cancel, sliding back from the release offset to the origin.
void PianoRollComponent::paintCopyGhosts(juce::Graphics& g) {
    const auto* clip = doc_ != nullptr ? doc_->getClip(clipId_) : nullptr;
    if (clip == nullptr)
        return;

    const std::vector<NoteOrigin>* notes = nullptr;
    double deltaBeats = 0.0;
    int deltaRows = 0;
    float fade = 1.0f;
    float slideBack = 0.0f; // 0 = at the preview offset, 1 = back at the origin
    if (dragMode_ == DragMode::Move && copyDrag_) {
        notes = &dragNotes_;
        deltaBeats = previewDeltaBeats_;
        deltaRows = previewDeltaPitch_;
    } else if (ghostSettle_.has_value()) {
        notes = &ghostSettle_->notes;
        deltaBeats = ghostSettle_->deltaBeats;
        deltaRows = ghostSettle_->deltaRows;
        fade = 1.0f - ghostSettle_->t;
        slideBack = ghostSettle_->dropped ? 0.0f : ghostSettle_->t;
    }
    if (notes == nullptr)
        return;

    const juce::Colour accent = synth::theme::themeOf(*this).colors.accent;

    for (const auto& origin : *notes) {
        const int pitch = rowShiftedPitch(origin.pitch, deltaRows);
        auto rect =
            computeNoteRect(clip->startBeat + origin.startBeat + deltaBeats, origin.lengthBeats, pitch).toFloat();
        if (slideBack > 0.0f) {
            const auto home =
                computeNoteRect(clip->startBeat + origin.startBeat, origin.lengthBeats, origin.pitch).toFloat();
            rect.setPosition(rect.getX() + (home.getX() - rect.getX()) * slideBack,
                             rect.getY() + (home.getY() - rect.getY()) * slideBack);
        }
        if (rect.getRight() < 0.0f || rect.getX() > (float)getWidth())
            continue; // same offscreen cull paintNote uses

        const auto* source = doc_->getNote(origin.id);
        const auto paint = resolveNoteColourFor(pitch, origin.velocity, false, source != nullptr && source->muted);
        const auto body = rect.reduced(0.5f, 1.0f);

        g.setColour(juce::Colours::black.withAlpha(0.10f * fade));
        g.fillRoundedRectangle(body.expanded(2.0f).translated(0.0f, 3.0f), 4.0f);
        g.setColour(juce::Colours::black.withAlpha(0.18f * fade));
        g.fillRoundedRectangle(body.expanded(1.0f).translated(0.0f, 2.0f), 3.0f);
        g.setColour(paint.fill.withMultipliedAlpha(kGhostOpacity * fade));
        g.fillRoundedRectangle(body, 2.0f);
        g.setColour(accent.withAlpha(fade));
        g.drawRoundedRectangle(body, 2.0f, 1.0f);
    }
}

} // namespace synth::ui
