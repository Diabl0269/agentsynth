// PianoRollComponent — construction/teardown, clip open/close entry points, and the roll's OWN
// horizontal (beat<->x) geometry mapping. The class itself is declared in PianoRollComponent.h;
// painting, editing, mouse handling, audition, clipboard, scale assist and zoom each live in a
// sibling PianoRoll<Concern>.cpp unit in this directory (see PianoRollComponent.h's class comment
// for the coordinate-system contract every unit shares).

#include "PianoRollComponent.h"
#include "PianoRollInternal.h"
#include <algorithm>
#include <cmath>
#include <set>

namespace synth::ui {

using namespace synth::ui::detail;

namespace {
int medianPitchOf(const std::vector<synth::MidiNote>& notes) {
    if (notes.empty())
        return 60;
    std::vector<int> pitches;
    pitches.reserve(notes.size());
    for (const auto& n : notes)
        pitches.push_back(n.pitch);
    std::sort(pitches.begin(), pitches.end());
    return pitches[pitches.size() / 2];
}
} // namespace

//==============================================================================
PianoRollComponent::PianoRollComponent(TimelineViewState& viewState)
    : viewState_(viewState) {
    setComponentID("pianoRollComponent");
    setInterceptsMouseClicks(true, false);
    setWantsKeyboardFocus(true);
    // No scale context yet, so this just fills visiblePitches_ with every pitch — see the class
    // comment and rebuildVisiblePitches.
    rebuildVisiblePitches();

    addChildComponent(scalePanel_); // starts INVISIBLE; the header button / persisted flag show it
    scalePanel_.onScaleChanged = [this](std::optional<synth::MusicalScale> scale) {
        if (clipId_.isValid())
            clipScaleMemory_[clipId_].scale = scale;
        pushScaleContextFromMemory();
    };
    scalePanel_.onPitchVisibilityChanged = [this](bool on) {
        if (clipId_.isValid())
            clipScaleMemory_[clipId_].pitchVisibilityOn = on;
        pushScaleContextFromMemory();
    };
    // Pitch-quantize has NO panel button any more — the header chip and "pianoRollQuantisePitches"
    // are its only entry points (see the ScaleAssistPanel class comment).
    scalePanel_.onGenerate = [this](int minPitch, int maxPitch, bool addToExisting) {
        if (!clipId_.isValid())
            return;
        // Copied out rather than kept as a pointer into the map: generateRandomNotesIntoClip
        // mutates the doc (and can therefore repaint/rebuild things this lambda has no business
        // reasoning about the lifetime of), so the scale it reads must not depend on the map
        // entry surviving the call.
        const auto scale = activeScaleForOpenClip();
        juce::Random rng; // default-seeded — the panel's Generate button always wants a fresh draw
        generateRandomNotesIntoClip(scale.has_value() ? &*scale : nullptr, minPitch, maxPitch, rng, addToExisting);
    };
}

PianoRollComponent::~PianoRollComponent() {
    // A note this roll auditioned must not outlive it: the owner's synth has no way to learn the
    // component went away, so the noteOff has to be emitted here (see onAuditionNote's contract).
    // Before the animation teardown, because the callback is the thing with a deadline.
    stopAudition();
    keysColumnPressing_ = false; // no repaint from a dying component; the note-off above is the point
    // The AnimationDriver's callbacks capture 'this' indirectly (see setScalePanelVisible); an
    // animation still running when this object goes away would call back into a destroyed
    // component, exactly the hazard ModuleLibraryComponent's own destructor guards against.
    if (scalePanelVblankUpdater_.has_value())
        scalePanelAnim_.stop(*scalePanelVblankUpdater_);
}

//==============================================================================
// ---- Entry/exit ----

void PianoRollComponent::openClip(synth::ClipId id) {
    dragMode_ = DragMode::None;
    pendingEmptyClick_ = false;
    resizeNotes_.clear();
    resizeUnquantized_ = false;
    moveUnquantized_ = false;
    cmdToggleNote_ = {};
    // A note auditioned in the OLD clip has no mouse-up coming — this IS the end of that gesture.
    stopAudition();
    endKeysColumnPress();
    autoScrollTimer_.stopTimer(); // a drag from the PREVIOUS clip cannot still be scrolling this one
    selection_.clear();
    // The line has to be re-announced against the new framing before it is drawn again.
    hasPlayheadX_ = false;
    // The hovered cut belonged to a note in the OLD clip. (The clipboard deliberately survives —
    // see copySelectedNotes.)
    hasSplitPreview_ = false;
    splitPreviewNote_ = {};
    // No animation across a clip switch — an in-flight scale-panel slide snaps to its target
    // instantly (the panel's own open/closed state is unrelated to which clip is open).
    if (scalePanelAnim_.isRunning())
        finishScalePanelAnimation();

    if (doc_ == nullptr) {
        clipId_ = {};
        rebuildVisiblePitches();
        return;
    }
    const auto* clip = doc_->getClip(id);
    if (clip == nullptr) {
        clipId_ = {};
        rebuildVisiblePitches();
        return;
    }
    clipId_ = id;

    const int visibleRows = std::max(1, (getHeight() - canvasTop()) / (int)pixelsPerSemitone_);
    const int median = medianPitchOf(clip->notes);
    firstVisiblePitch_ = juce::jlimit(0, 127, median + visibleRows / 2);
    // A fresh, freshly-computed landing for the clip just opened — never a continuation of
    // wherever the PREVIOUS clip had scrolled to. rebuildVisiblePitches (below) re-derives
    // topRowPosition_ from firstVisiblePitch_ against THIS clip's (possibly different) row set,
    // preserving topRowPosition_'s fractional part across that remap — 0 here, so the landing
    // comes out on exactly the row firstVisiblePitch_ names, not offset by some leftover scroll.
    topRowPosition_ = 0.0;
    // The new clip's notes are what pitch-visibility mode keeps visible alongside the scale, so the
    // row set has to be rebuilt against THIS clip before anything below reads visiblePitches_.
    rebuildVisiblePitches();

    // Restores THIS clip's remembered scale/pitch-visibility (or "No scale" for a clip never
    // opened before) into the panel and the roll's own scale context — rebuilds visiblePitches_
    // again, correctly this time (the call above ran against whichever clip was open previously).
    restoreScaleMemoryForOpenClip();

    // Frame the clip: its start at the keys column's right edge, zoomed so the whole clip fits the
    // grid width (subject to the shared zoom clamps).
    const double gridWidth = std::max(1.0, (double)(getWidth() - leftGutterWidth()));
    const double fitted = clip->lengthBeats > 0.0 ? gridWidth / clip->lengthBeats : rollView_.pixelsPerBeat;
    setHorizontalView(fitted, clip->startBeat);
    repaint();
}

void PianoRollComponent::closeRoll() {
    // BEFORE clipId_ is cleared, exactly like openClip's ordering — and load-bearing, not just
    // symmetry: stopAudition() calls out to the owner, and the owner resolves which track to send the
    // note-off to FROM THE OPEN CLIP. Clearing clipId_ first made isOpen() false while that note-off
    // was still in flight, so the owner dropped it and the note hung until bypass (audition notes are
    // deliberately exempt from every positional flush — see TimelineMidiSourceModule).
    stopAudition();
    endKeysColumnPress();
    clipId_ = {};
    selection_.clear();
    dragMode_ = DragMode::None;
    pendingEmptyClick_ = false;
    resizeNotes_.clear();
    resizeUnquantized_ = false;
    moveUnquantized_ = false;
    cmdToggleNote_ = {};
    autoScrollTimer_.stopTimer(); // no clip left for a drag to be scrolling

    hasPlayheadX_ = false;
    hasSplitPreview_ = false;
    splitPreviewNote_ = {};
    // No animation across the roll closing — see openClip's identical guard.
    if (scalePanelAnim_.isRunning())
        finishScalePanelAnimation();
    repaint();
}

void PianoRollComponent::requestClose() {
    // Self-contained: closes immediately (so isOpen() reflects it even with no owner listening —
    // a bare PianoRollComponent in a test) AND notifies the owner, which is what makes
    // TimelinePanelComponent swap the clip-lane area back in.
    closeRoll();
    if (onCloseRequested)
        onCloseRequested();
}

void PianoRollComponent::refreshFromDoc() {
    if (!clipId_.isValid())
        return;
    const auto* clip = doc_ != nullptr ? doc_->getClip(clipId_) : nullptr;
    if (clip == nullptr) {
        requestClose(); // the edited clip is gone — the roll closes itself
        return;
    }

    std::vector<synth::NoteId> alive;
    alive.reserve(clip->notes.size());
    for (const auto& note : clip->notes)
        alive.push_back(note.id);
    selection_.retainOnly(alive);
    // Same pruning for the hovered cut: a Split click removes nothing, but an undo (or another
    // view's edit) can delete the note the preview line is drawn on.
    if (hasSplitPreview_ && doc_->getNote(splitPreviewNote_) == nullptr) {
        hasSplitPreview_ = false;
        splitPreviewNote_ = {};
    }
    // A note add/remove can change which out-of-scale pitches pitch-visibility mode is keeping
    // visible on the note's account alone.
    rebuildVisiblePitches();
    repaint();
}

//==============================================================================
// ---- Geometry: the roll's OWN horizontal mapping ----

double PianoRollComponent::beatToX(double absBeat) const noexcept {
    return (double)leftGutterWidth() + rollView_.beatToX(absBeat);
}

double PianoRollComponent::xToBeat(double x) const noexcept { return rollView_.xToBeat(x - (double)leftGutterWidth()); }

void PianoRollComponent::setHorizontalView(double pixelsPerBeat, double firstVisibleBeat) {
    if (!std::isfinite(pixelsPerBeat) || !std::isfinite(firstVisibleBeat))
        return;
    rollView_.pixelsPerBeat =
        std::clamp(pixelsPerBeat, TimelineViewState::kMinPixelsPerBeat, TimelineViewState::kMaxPixelsPerBeat);
    rollView_.firstVisibleBeat = std::max(0.0, firstVisibleBeat);
    repaint();
    if (onHorizontalViewChanged)
        onHorizontalViewChanged();
}

void PianoRollComponent::setPixelsPerSemitone(double pixelsPerSemitone) {
    if (!std::isfinite(pixelsPerSemitone))
        return;
    pixelsPerSemitone_ = std::clamp(pixelsPerSemitone, kMinPixelsPerSemitone, kMaxPixelsPerSemitone);
    repaint();
}

size_t PianoRollComponent::nearestVisibleRowIndex(int pitch) const noexcept {
    if (visiblePitches_.empty())
        return 0;
    const auto it = std::lower_bound(visiblePitches_.begin(), visiblePitches_.end(), pitch);
    if (it == visiblePitches_.end())
        return visiblePitches_.size() - 1;
    const size_t idx = (size_t)std::distance(visiblePitches_.begin(), it);
    if (*it == pitch || it == visiblePitches_.begin())
        return idx;
    // *it is the smallest visible pitch >= `pitch`; compare it against its neighbour below to find
    // whichever row is actually closer (ties go to the lower row, matching MusicalScale::snapPitch).
    const int upper = *it;
    const int lower = visiblePitches_[idx - 1];
    return (pitch - lower <= upper - pitch) ? idx - 1 : idx;
}

int PianoRollComponent::rowShiftedPitch(int originPitch, long long rowDelta) const noexcept {
    if (visiblePitches_.empty())
        return juce::jlimit(0, 127, originPitch);
    const long long total = (long long)visiblePitches_.size();
    const long long originRow = (long long)nearestVisibleRowIndex(originPitch);
    const long long newRow = std::clamp(originRow + rowDelta, 0LL, total - 1);
    return visiblePitches_[(size_t)newRow];
}

int PianoRollComponent::yForPitch(int pitch) const noexcept {
    if (visiblePitches_.empty())
        return canvasTop();
    // topRowPosition_ IS the (fractional) row index whose top edge sits at y == canvasTop() (see
    // the class comment) — the exact same formula the old int-only firstRow used, with firstRow
    // simply replaced by the continuous anchor. An integral topRowPosition_ reproduces today's
    // pixel-for-pixel result (llround(N * ps) == llround((double)N * ps) for integer N).
    const double pitchRow = (double)nearestVisibleRowIndex(pitch);
    return canvasTop() + (int)std::llround((topRowPosition_ - pitchRow) * pixelsPerSemitone_);
}

int PianoRollComponent::pitchForY(int y) const noexcept {
    if (visiblePitches_.empty())
        return firstVisiblePitch_;
    // Inverts yForPitch's formula: solving y == canvasTop() + (topRowPosition_ - row) *
    // pixelsPerSemitone_ for the INTEGER row whose drawn rect (see yForPitch) contains `y` gives
    // row == ceil(topRowPosition_ - (y - canvasTop()) / pixelsPerSemitone_) — ceil, not floor,
    // because each row's rect starts AT its own y and extends towards increasing y (decreasing row
    // index — pitch decreases as y increases). This pins identical to the old
    // "firstRow - floor((y-canvasTop())/ps)" when topRowPosition_ is itself an integer N:
    // ceil(N - x) == N - floor(x) for integer N and any real x. kRowEpsilon guards the case where
    // floating-point noise from the divide pushes a should-be-exact row boundary a hair past the
    // integer it should ceil down to.
    const double rowsBelowTop = (double)(y - canvasTop()) / pixelsPerSemitone_;
    const long long total = (long long)visiblePitches_.size();
    const long long targetRow =
        std::clamp((long long)std::ceil(topRowPosition_ - rowsBelowTop - kRowEpsilon), 0LL, total - 1);
    return visiblePitches_[(size_t)targetRow];
}

bool PianoRollComponent::setTopRowPosition(double raw) noexcept {
    if (visiblePitches_.empty()) {
        topRowPosition_ = 0.0;
        return false;
    }
    const double clamped = std::clamp(raw, minTopRowPosition(), maxTopRowPosition());
    const bool moved = clamped != topRowPosition_;
    topRowPosition_ = clamped;
    const long long total = (long long)visiblePitches_.size();
    // floor, deliberately: firstVisiblePitch_ means "the row whose top edge is at or above the
    // header line", i.e. the row a user would call the top one — and flooring is the only
    // derivation under which sub-row scrolling leaves it put until a full row has actually been
    // crossed. Any integer view of a fractional position quantizes SOMEWHERE, so symmetry claims
    // about scrolling belong to the continuous position / pixel mapping (yForPitch), never to this
    // derived legacy value.
    const long long row = std::clamp((long long)std::floor(topRowPosition_), 0LL, total - 1);
    firstVisiblePitch_ = visiblePitches_[(size_t)row];
    return moved;
}

double PianoRollComponent::maxTopRowPosition() const noexcept {
    return visiblePitches_.empty() ? 0.0 : (double)visiblePitches_.size() - 1.0;
}

double PianoRollComponent::minTopRowPosition() const noexcept {
    if (visiblePitches_.empty())
        return 0.0;
    const double totalRows = (double)visiblePitches_.size();
    // The grid's height expressed in ROWS, at the CURRENT (possibly non-integer) zoom — the same
    // quantity paintGrid/paintKeysColumn derive (there, floored to an int row-height for their own
    // loop bounds; here kept exact, since a fractional row of headroom is exactly what should let
    // the lowest row land a fraction short of the bottom edge rather than snapping early).
    const double gridRows =
        pixelsPerSemitone_ > 0.0 ? (double)std::max(0, getHeight() - canvasTop()) / pixelsPerSemitone_ : 0.0;
    return std::max(0.0, maxTopRowPosition() - std::max(0.0, totalRows - gridRows));
}

void PianoRollComponent::rebuildVisiblePitches() {
    // Capture the pre-rebuild anchor — the pitch currently at the (possibly fractional) top-row
    // position, plus exactly how far past its row we had scrolled — before visiblePitches_ is
    // cleared out from under it. A scale-context change or a note add/remove must not snap the
    // user's scroll to a whole row: it re-lands on the SAME pitch, at the SAME fractional offset,
    // remapped onto whatever row that pitch occupies in the new set.
    const double oldFraction = topRowPosition_ - std::floor(topRowPosition_);
    const int anchorPitch = firstVisiblePitch_;

    visiblePitches_.clear();
    // Empty isInScale_ or visibility off: no filtering at all, so a scale that only affects
    // paintNote's colouring never touches which rows exist.
    const bool filtering = (bool)isInScale_ && pitchVisibilityOn_;
    if (!filtering) {
        visiblePitches_.reserve(128);
        for (int p = 0; p <= 127; ++p)
            visiblePitches_.push_back(p);
    } else {
        // A note the open clip already has must never become unreachable by turning visibility on,
        // scale membership notwithstanding.
        std::set<int> notePitches;
        if (doc_ != nullptr && clipId_.isValid()) {
            if (const auto* clip = doc_->getClip(clipId_))
                for (const auto& note : clip->notes)
                    notePitches.insert(note.pitch);
        }
        for (int p = 0; p <= 127; ++p)
            if (isInScale_(p) || notePitches.count(p) > 0)
                visiblePitches_.push_back(p);
        if (visiblePitches_.empty()) {
            // A degenerate scale (mask == 0, no notes) would otherwise leave the grid with literally
            // nothing to show — fall back to everything rather than an unusable blank roll.
            for (int p = 0; p <= 127; ++p)
                visiblePitches_.push_back(p);
        }
    }

    // setTopRowPosition re-derives firstVisiblePitch_ from the result and no-ops safely (leaving
    // firstVisiblePitch_ untouched) if visiblePitches_ somehow ended up empty — the same guard the
    // old "if (!visiblePitches_.empty())" line used.
    setTopRowPosition((double)nearestVisibleRowIndex(anchorPitch) + oldFraction);
}

bool PianoRollComponent::isScaleFilterOn() const noexcept {
    if (!clipId_.isValid())
        return false;
    const auto it = clipScaleMemory_.find(clipId_);
    return it != clipScaleMemory_.end() && it->second.pitchVisibilityOn;
}

void PianoRollComponent::toggleScaleFilter() {
    if (!clipId_.isValid())
        return;
    const bool next = !isScaleFilterOn();
    // The clip's memory is the single source of truth (see clipScaleMemory_); the panel's checkbox and
    // this chip are both VIEWS of it, which is what stops the two from drifting apart.
    clipScaleMemory_[clipId_].pitchVisibilityOn = next;
    pushScaleContextFromMemory();
    // Reflect it back into the panel. setSelection fires NO callback by contract — it is a reflection
    // of state the owner already applied, not a user edit — so this cannot loop back through
    // onPitchVisibilityChanged into here again.
    scalePanel_.setSelection(activeScaleForOpenClip(), next);
    repaint(); // the chip's lit state, plus whatever rows just appeared or collapsed
}

void PianoRollComponent::setScaleContext(std::function<bool(int)> isInScale, bool pitchVisibilityOn) {
    isInScale_ = std::move(isInScale);
    pitchVisibilityOn_ = pitchVisibilityOn;
    rebuildVisiblePitches();
    repaint();
}

//==============================================================================
// ---- Simple accessors (moved out of the header — see PianoRollComponent.h for each contract) ----
void PianoRollComponent::setTimelineDoc(synth::TimelineDoc* doc) noexcept { doc_ = doc; }
synth::TimelineDoc* PianoRollComponent::getTimelineDoc() const noexcept { return doc_; }
void PianoRollComponent::setUndoManager(AppUndoManager* undoManager) noexcept { undoManager_ = undoManager; }
AppUndoManager* PianoRollComponent::getUndoManager() const noexcept { return undoManager_; }
void PianoRollComponent::setTransport(synth::TransportService* transport) noexcept { transport_ = transport; }

bool PianoRollComponent::isOpen() const noexcept { return clipId_.isValid(); }
synth::ClipId PianoRollComponent::getClipId() const noexcept { return clipId_; }

const TimelineViewState& PianoRollComponent::getRollViewState() const noexcept { return rollView_; }
double PianoRollComponent::getPixelsPerBeat() const noexcept { return rollView_.pixelsPerBeat; }
double PianoRollComponent::getFirstVisibleBeat() const noexcept { return rollView_.firstVisibleBeat; }
double PianoRollComponent::getPixelsPerSemitone() const noexcept { return pixelsPerSemitone_; }

int PianoRollComponent::leftGutterWidth() const noexcept {
    return (int)std::llround((double)scalePanelOpenProgress_ * (double)kScalePanelWidth) + kKeysColumnWidth;
}

bool PianoRollComponent::isRowFilterActive() const noexcept { return pitchVisibilityOn_ && (bool)isInScale_; }

int PianoRollComponent::getFirstVisiblePitchForTest() const noexcept { return firstVisiblePitch_; }
double PianoRollComponent::getTopRowPositionForTest() const noexcept { return topRowPosition_; }
void PianoRollComponent::setTopRowPositionForTest(double position) noexcept { setTopRowPosition(position); }
double PianoRollComponent::getMinTopRowPositionForTest() const noexcept { return minTopRowPosition(); }
double PianoRollComponent::getMaxTopRowPositionForTest() const noexcept { return maxTopRowPosition(); }
const std::vector<int>& PianoRollComponent::getVisiblePitchesForTest() const noexcept { return visiblePitches_; }

} // namespace synth::ui
