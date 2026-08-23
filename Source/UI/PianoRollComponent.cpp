#include "PianoRollComponent.h"
#include "../AppUndoManager.h"
#include "../ShortcutManager.h"
#include "../Transport/TransportService.h"
#include "EdgeAutoScroll.h"
#include "ScrollPolicy.h"
#include "Theme/AppLookAndFeel.h"
// For the SHARED three-level grid colour policy (GridLineLevel / gridLineColourFor /
// gridLevelIsReadable). The lane header is where it lives because the timeline's own vertical grid
// is painted by TimelinePanelComponent over that component's rect — see the comment block there.
#include "TimelineClipLaneArea.h"
#include "ToolCursors.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <set>

namespace synth::ui {

namespace {
// Wheel tuning. kZoomWheelSensitivity mirrors TimelinePanelComponent::mouseWheelMove's own constant
// (exponential in deltaY, so equal-and-opposite gestures cancel exactly) and
// kScrollPixelsPerWheelUnit mirrors its scroll constant — both duplicated rather than shared
// because they are one-line cosmetic tunings private to a different translation unit.
constexpr double kZoomWheelSensitivity = 2.0;
constexpr double kScrollPixelsPerWheelUnit = 200.0;
constexpr double kPitchScrollSemitonesPerWheelUnit = 3.0;

// Edge-auto-scroll tuning (see EdgeAutoScroll.h). Horizontal mirrors
// TimelineClipLaneArea's kEdgeAutoScrollMaxPxPerTick exactly — the roll's own drag should feel
// identical. Vertical is ROWS, not pixels (visiblePitches_ can collapse rows to less than
// pixelsPerSemitone_ apart in screen terms — a row is the unit that means something), and ~1 row
// per tick at kEdgeScrollHz is a brisk-but-followable walk up/down the keyboard.
constexpr double kEdgeAutoScrollMaxPxPerTick = 18.0;
constexpr double kEdgeAutoScrollMaxRowsPerTick = 1.0;

// The per-level gridline density guard now lives with the rest of the grid's colour/visibility
// policy in TimelineClipLaneArea.h (synth::ui::kMinGridLinePixels / gridLevelIsReadable), shared
// with the surface that paints the timeline lanes' grid so the two can never disagree about when a
// level is too dense to read.

// ---- Default surface-key bindings (used when no ShortcutManager is installed) ----
// One table so the fallbacks and the ShortcutManager action ids sit side by side and cannot drift:
// every entry is BOTH the id keyPressed() resolves and the key it falls back to. A sibling phase
// adds the same ids (and the same defaults) to ShortcutManager::resetToDefaults(); nothing here
// requires that to have happened, because getBinding() answers an unknown id with an invalid
// KeyPress and an invalid binding simply matches nothing.
juce::KeyPress plainKey(int keyCode) noexcept { return juce::KeyPress(keyCode, juce::ModifierKeys::noModifiers, 0); }
juce::KeyPress modKey(int keyCode, int modifierFlags) noexcept {
    return juce::KeyPress(keyCode, juce::ModifierKeys(modifierFlags), 0);
}

bool isBlackKeyPitchClass(int pitchClass) noexcept {
    switch (pitchClass) {
    case 1:
    case 3:
    case 6:
    case 8:
    case 10:
        return true;
    default:
        return false;
    }
}

// Tolerance for the "is this cut strictly inside the note" / "does this paste still fit" beat
// comparisons. Beats are doubles that have been through a snap multiply-divide, so an exact
// comparison would reject a cut that IS on a grid line by a few ULPs.
constexpr double kBeatEpsilon = 1.0e-9;

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
    scalePanel_.onQuantizePitches = [this] {
        if (!clipId_.isValid())
            return;
        const auto it = clipScaleMemory_.find(clipId_);
        if (it != clipScaleMemory_.end() && it->second.scale.has_value())
            quantisePitchesToScale(*it->second.scale);
    };
    scalePanel_.onGenerate = [this](int minPitch, int maxPitch) {
        if (!clipId_.isValid())
            return;
        // Copied out rather than kept as a pointer into the map: generateRandomNotesIntoClip
        // mutates the doc (and can therefore repaint/rebuild things this lambda has no business
        // reasoning about the lifetime of), so the scale it reads must not depend on the map
        // entry surviving the call.
        synth::MusicalScale scaleStorage;
        const synth::MusicalScale* scalePtr = nullptr;
        const auto it = clipScaleMemory_.find(clipId_);
        if (it != clipScaleMemory_.end() && it->second.scale.has_value()) {
            scaleStorage = *it->second.scale;
            scalePtr = &scaleStorage;
        }
        juce::Random rng; // default-seeded — the panel's Generate button always wants a fresh draw
        generateRandomNotesIntoClip(scalePtr, minPitch, maxPitch, rng);
    };
}

//==============================================================================
// ---- Entry/exit ----

void PianoRollComponent::openClip(synth::ClipId id) {
    dragMode_ = DragMode::None;
    pendingEmptyClick_ = false;
    autoScrollTimer_.stopTimer(); // a drag from the PREVIOUS clip cannot still be scrolling this one
    selection_.clear();
    // The line has to be re-announced against the new framing before it is drawn again.
    hasPlayheadX_ = false;
    // The hovered cut belonged to a note in the OLD clip. (The clipboard deliberately survives —
    // see copySelectedNotes.)
    hasSplitPreview_ = false;
    splitPreviewNote_ = {};

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

    const int visibleRows = std::max(1, (getHeight() - kHeaderHeight) / (int)pixelsPerSemitone_);
    const int median = medianPitchOf(clip->notes);
    firstVisiblePitch_ = juce::jlimit(0, 127, median + visibleRows / 2);
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
    clipId_ = {};
    selection_.clear();
    dragMode_ = DragMode::None;
    pendingEmptyClick_ = false;
    autoScrollTimer_.stopTimer(); // no clip left for a drag to be scrolling

    hasPlayheadX_ = false;
    hasSplitPreview_ = false;
    splitPreviewNote_ = {};
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
        return kHeaderHeight;
    const long long firstRow = (long long)nearestVisibleRowIndex(firstVisiblePitch_);
    const long long pitchRow = (long long)nearestVisibleRowIndex(pitch);
    return kHeaderHeight + (int)std::llround((double)(firstRow - pitchRow) * pixelsPerSemitone_);
}

int PianoRollComponent::pitchForY(int y) const noexcept {
    if (visiblePitches_.empty())
        return firstVisiblePitch_;
    const long long firstRow = (long long)nearestVisibleRowIndex(firstVisiblePitch_);
    const long long rowOffset = (long long)std::floor((double)(y - kHeaderHeight) / pixelsPerSemitone_);
    const long long total = (long long)visiblePitches_.size();
    const long long targetRow = std::clamp(firstRow - rowOffset, 0LL, total - 1);
    return visiblePitches_[(size_t)targetRow];
}

void PianoRollComponent::rebuildVisiblePitches() {
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

    if (!visiblePitches_.empty())
        firstVisiblePitch_ = visiblePitches_[nearestVisibleRowIndex(firstVisiblePitch_)];
}

void PianoRollComponent::setScaleContext(std::function<bool(int)> isInScale, bool pitchVisibilityOn) {
    isInScale_ = std::move(isInScale);
    pitchVisibilityOn_ = pitchVisibilityOn;
    rebuildVisiblePitches();
    repaint();
}

//==============================================================================
// ---- Scale assist panel ----

namespace {
// The roll's own "was the panel open" flag — the panel's user scales persist under their OWN key
// (ScaleAssistPanel.h's kUserScalesPropertyKey), set directly via setPropertiesFile below.
constexpr const char* kScalePanelVisiblePropertyKey = "pianoRollScalePanelVisible";
} // namespace

void PianoRollComponent::setPropertiesFile(juce::PropertiesFile* props) {
    propertiesFile_ = props;
    scalePanel_.setPropertiesFile(props);
    const bool visible = props != nullptr && props->getBoolValue(kScalePanelVisiblePropertyKey, false);
    setScalePanelVisible(visible);
}

void PianoRollComponent::setScalePanelVisible(bool visible) {
    if (scalePanel_.isVisible() == visible)
        return; // restoring the default (closed) on a fresh PropertiesFile costs nothing
    scalePanel_.setVisible(visible);
    resized(); // the gutter width just changed — carve the panel/keys-column/grid split again
    repaint();
    // leftGutterWidth() just moved by kScalePanelWidth, and that IS the roll's horizontal mapping
    // (beatToX/xToBeat offset by it) — the same seam a wheel zoom/scroll already fires.
    // TimelinePanelComponent's wiring re-issues the ruler's mapping-override offset from this while
    // the roll is open, so both the header button's toggle and setPropertiesFile's restore below
    // keep the ruler's ticks/hit-testing lined up with the grid.
    if (onHorizontalViewChanged)
        onHorizontalViewChanged();
    if (propertiesFile_ != nullptr) {
        propertiesFile_->setValue(kScalePanelVisiblePropertyKey, visible);
        propertiesFile_->saveIfNeeded();
    }
}

void PianoRollComponent::toggleScalePanel() { setScalePanelVisible(!scalePanel_.isVisible()); }

void PianoRollComponent::pushScaleContextFromMemory() {
    std::optional<synth::MusicalScale> scale;
    bool pitchVisibilityOn = false;
    if (clipId_.isValid()) {
        if (const auto it = clipScaleMemory_.find(clipId_); it != clipScaleMemory_.end()) {
            scale = it->second.scale;
            pitchVisibilityOn = it->second.pitchVisibilityOn;
        }
    }
    if (scale.has_value()) {
        const synth::MusicalScale resolved = *scale;
        setScaleContext([resolved](int pitch) { return resolved.contains(pitch); }, pitchVisibilityOn);
    } else {
        setScaleContext({}, false);
    }
}

void PianoRollComponent::restoreScaleMemoryForOpenClip() {
    std::optional<synth::MusicalScale> scale;
    bool pitchVisibilityOn = false;
    if (const auto it = clipScaleMemory_.find(clipId_); it != clipScaleMemory_.end()) {
        scale = it->second.scale;
        pitchVisibilityOn = it->second.pitchVisibilityOn;
    }
    // A clip never opened before has no entry yet, and reads back exactly as "No scale" /
    // visibility off without ever inserting one — see clipScaleMemory_'s class comment.
    scalePanel_.setSelection(scale, pitchVisibilityOn);
    if (scale.has_value()) {
        const synth::MusicalScale resolved = *scale;
        setScaleContext([resolved](int pitch) { return resolved.contains(pitch); }, pitchVisibilityOn);
    } else {
        setScaleContext({}, false);
    }
}

void PianoRollComponent::quantisePitchesToScale(const synth::MusicalScale& scale) {
    if (doc_ == nullptr || !clipId_.isValid())
        return;
    const auto* clip = doc_->getClip(clipId_);
    if (clip == nullptr)
        return;

    // Selected notes if any; every note in the clip otherwise — the same "or all notes when
    // nothing is selected" contract the Q button's Shift+click quantise follows.
    std::vector<synth::NoteId> ids = selection_.getSelected();
    if (ids.empty()) {
        ids.reserve(clip->notes.size());
        for (const auto& note : clip->notes)
            ids.push_back(note.id);
    }
    if (ids.empty())
        return;

    auto mutate = [this, ids, scale] {
        for (auto id : ids) {
            const auto* note = doc_->getNote(id);
            if (note == nullptr)
                continue;
            const int snapped = scale.snapPitch(note->pitch);
            if (snapped != note->pitch)
                doc_->moveNote(id, note->startBeat, snapped);
        }
    };
    // recordTimelineChange itself pushes no undo step when the mutation changed nothing (every
    // note already in scale) — the "no-op pushes nothing" half of the contract needs no extra
    // code here.
    if (undoManager_)
        undoManager_->recordTimelineChange(*doc_, mutate);
    else
        mutate();

    // Selection is deliberately left untouched: this only ever moves pitches.
    repaint();
}

void PianoRollComponent::generateRandomNotesIntoClip(const synth::MusicalScale* scale, int minPitch, int maxPitch,
                                                     juce::Random& rng) {
    if (doc_ == nullptr || !clipId_.isValid())
        return;
    const auto* clip = doc_->getClip(clipId_);
    if (clip == nullptr)
        return;

    // The RAW division, ignoring the on/off toggle (the same "clean up notes drawn free-hand"
    // reasoning isQuantiseEnabled documents), falling back to a sixteenth when that division is
    // Off/0 — generation always wants a concrete grid step to place notes on.
    double gridBeats = viewState_.divisionBeatsRaw(currentBeatsPerBar());
    if (gridBeats <= 0.0)
        gridBeats = 0.25;

    const auto notes = synth::generateRandomNotes(clip->lengthBeats, gridBeats, minPitch, maxPitch, scale, rng);

    // "Generate" means "replace": ONE mutation removes every existing note and adds the fresh
    // batch, so undo restores the old contents in a single step rather than a clear-then-a-paste.
    std::vector<synth::NoteId> newIds;
    auto mutate = [this, notes, &newIds] {
        doc_->clearNotes(clipId_);
        newIds.clear();
        for (const auto& note : notes)
            if (const auto id = doc_->addNote(clipId_, note); id.isValid())
                newIds.push_back(id);
    };
    if (undoManager_)
        undoManager_->recordTimelineChange(*doc_, mutate);
    else
        mutate();

    selection_.setSelection(newIds);
    repaint();
}

synth::ui::NotePaint PianoRollComponent::resolveNoteColourFor(int pitch, int velocity, bool selected,
                                                              bool muted) const {
    // Fallback discipline matches every other paint helper in this file: no AppLookAndFeel means no
    // theme to read, so the resolver gets a plain default-constructed Colors rather than reaching
    // into a null LookAndFeel.
    synth::theme::Colors colors;
    if (auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel()))
        colors = lf->getTheme().colors;
    const bool outOfScale = (bool)isInScale_ && !isInScale_(pitch);
    return synth::ui::resolveNoteColour(colors, pitch, velocity, selected, muted, outOfScale, overrides_);
}

synth::ui::NotePaint PianoRollComponent::notePaintFor(const synth::MidiNote& note) const {
    return resolveNoteColourFor(note.pitch, note.velocity, selection_.contains(note.id), note.muted);
}

juce::String PianoRollComponent::keyLabelFor(int pitch, KeyLabelMode mode, int rowHeightPx) {
    if (pitch < 0 || pitch > 127)
        return {};
    const int pitchClass = ((pitch % 12) + 12) % 12;
    const int octave = pitch / 12 - 1;
    // Below the readability floor, or in OctavesOnly, only the C rows get a label at all — the
    // pre-scale-feature behaviour.
    if (rowHeightPx < 9 || mode == KeyLabelMode::OctavesOnly) {
        if (pitchClass != 0)
            return {};
        return "C" + juce::String(octave);
    }
    static const char* const kNoteNames[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    return juce::String(kNoteNames[pitchClass]) + juce::String(octave);
}

juce::Rectangle<int> PianoRollComponent::gridRegion() const noexcept {
    const int gutter = leftGutterWidth();
    return {gutter, kHeaderHeight, std::max(0, getWidth() - gutter), std::max(0, getHeight() - kHeaderHeight)};
}

juce::Rectangle<int> PianoRollComponent::computeNoteRect(double absStartBeat, double absLengthBeats, int pitch) const {
    const double x0 = beatToX(absStartBeat);
    const double x1 = beatToX(absStartBeat + absLengthBeats);
    const int left = (int)std::llround(x0);
    const int right = (int)std::llround(x1);
    return {left, yForPitch(pitch), std::max(right - left, 1), (int)pixelsPerSemitone_};
}

juce::Rectangle<int> PianoRollComponent::getNoteRect(synth::NoteId id) const {
    if (doc_ == nullptr || !clipId_.isValid())
        return {};
    const auto* clip = doc_->getClip(clipId_);
    if (clip == nullptr)
        return {};
    for (const auto& note : clip->notes)
        if (note.id == id)
            return computeNoteRect(clip->startBeat + note.startBeat, note.lengthBeats, note.pitch);
    return {};
}

std::vector<std::pair<synth::NoteId, juce::Rectangle<int>>> PianoRollComponent::collectNoteRects() const {
    std::vector<std::pair<synth::NoteId, juce::Rectangle<int>>> rects;
    if (doc_ == nullptr || !clipId_.isValid())
        return rects;
    const auto* clip = doc_->getClip(clipId_);
    if (clip == nullptr)
        return rects;
    for (const auto& note : clip->notes)
        rects.emplace_back(note.id, computeNoteRect(clip->startBeat + note.startBeat, note.lengthBeats, note.pitch));
    return rects;
}

std::optional<PianoRollComponent::NoteHit> PianoRollComponent::hitTestNote(juce::Point<int> pos) const {
    if (doc_ == nullptr || !clipId_.isValid())
        return std::nullopt;
    const auto* clip = doc_->getClip(clipId_);
    if (clip == nullptr)
        return std::nullopt;

    for (const auto& note : clip->notes) {
        const auto rect = computeNoteRect(clip->startBeat + note.startBeat, note.lengthBeats, note.pitch);
        if (!rect.contains(pos))
            continue;
        NoteHit hit{note.id, rect, pos.x >= rect.getRight() - kResizeZonePx};
        return hit;
    }
    return std::nullopt;
}

double PianoRollComponent::currentBeatsPerBar() const {
    double beatsPerBar = 4.0;
    if (transport_ != nullptr) {
        const auto snap = transport_->getPositionSnapshot();
        const double tsBeatsPerBar = (double)snap.timeSigNumerator * 4.0 / (double)std::max(1, snap.timeSigDenominator);
        if (tsBeatsPerBar > 0.0)
            beatsPerBar = tsBeatsPerBar;
    }
    return beatsPerBar;
}

double PianoRollComponent::currentGridBeats() const { return viewState_.divisionBeats(currentBeatsPerBar()); }

double PianoRollComponent::snappedBeatAt(double rawBeat) const {
    return viewState_.snapBeat(rawBeat, currentBeatsPerBar());
}

PianoRollComponent::NoteGeometry PianoRollComponent::effectiveGeometryFor(const synth::MidiNote& note) const {
    if (dragMode_ == DragMode::Move) {
        for (const auto& origin : dragNotes_)
            if (origin.id == note.id)
                return {origin.startBeat + previewDeltaBeats_, origin.lengthBeats,
                        rowShiftedPitch(origin.pitch, previewDeltaPitch_), origin.velocity};
    } else if (dragMode_ == DragMode::Resize && activeNote_ == note.id) {
        return {note.startBeat, previewLength_, note.pitch, note.velocity};
    } else if (dragMode_ == DragMode::VelocityScrub) {
        for (const auto& origin : dragNotes_)
            if (origin.id == note.id)
                return {note.startBeat, note.lengthBeats, note.pitch,
                        juce::jlimit(1, 127, origin.velocity + previewDeltaVelocity_)};
    }
    return {note.startBeat, note.lengthBeats, note.pitch, note.velocity};
}

//==============================================================================
// ---- Painting ----

void PianoRollComponent::paint(juce::Graphics& g) {
    using namespace synth::theme;
    juce::Colour bg = juce::Colours::black;
    if (auto* lf = dynamic_cast<AppLookAndFeel*>(&getLookAndFeel()))
        bg = lf->getTheme().colors.bg0;
    g.fillAll(bg);

    if (doc_ == nullptr || !clipId_.isValid())
        return;

    paintGrid(g);
    // Over the notes (both are about a note that is there or about to be), still under the keys
    // column and the header so everything is clipped by the same gutter.
    paintDrawPreview(g);
    paintSplitPreview(g);
    // Before the keys column and the header, so both clip the line the same way they clip a note
    // that has scrolled off to the left.
    paintPlayhead(g);
    paintKeysColumn(g);
    paintHeader(g);

    if (dragMode_ == DragMode::Marquee)
        paintMarquee(g);
}

PianoRollComponent::LineRange PianoRollComponent::visibleLineRange(double spacingBeats) const noexcept {
    LineRange range;
    // The shared density guard: too dense to read means this level is dropped entirely, never
    // drawn as a wall of touching pixels (see gridLevelIsReadable).
    if (!gridLevelIsReadable(spacingBeats, rollView_.pixelsPerBeat))
        return range;

    const auto grid = gridRegion();
    if (grid.isEmpty())
        return range;

    constexpr double kEps = 1.0e-9;
    const double leftBeat = xToBeat((double)grid.getX());
    const double rightBeat = xToBeat((double)grid.getRight());
    range.first = (long long)std::ceil(leftBeat / spacingBeats - kEps);
    range.last = (long long)std::floor(rightBeat / spacingBeats + kEps);
    return range;
}

int PianoRollComponent::getGridLineCountForTest(double spacingBeats) const noexcept {
    return visibleLineRange(spacingBeats).count();
}

void PianoRollComponent::paintGridLines(juce::Graphics& g, juce::Colour lineColour, juce::Colour background) {
    const auto grid = gridRegion();
    const float top = (float)grid.getY();
    const float bottom = (float)grid.getBottom();

    // Faintest level first so a bar line always wins a pixel it shares with a beat or sub-beat line.
    // Every level's colour comes from the ONE shared policy (TimelineClipLaneArea.h) rather than a
    // local alpha, so bar >= beat >= subdivision holds here and on the lanes by construction, and
    // the sub-beat level is a readable hint rather than the near-invisible hairline it used to be on
    // dark themes.
    const auto drawLevel = [&](double spacingBeats, GridLineLevel level) {
        const auto range = visibleLineRange(spacingBeats);
        if (range.count() == 0)
            return;
        g.setColour(gridLineColourFor(level, lineColour, background));
        for (long long i = range.first; i <= range.last; ++i) {
            const int x = (int)std::llround(beatToX((double)i * spacingBeats));
            if (x < grid.getX() || x > grid.getRight())
                continue;
            g.drawVerticalLine(x, top, bottom);
        }
    };

    // BEAT lines are drawn unconditionally (subject only to the density guard), including when the
    // snap division is COARSER than a beat — Snap::Bar/Whole/Half must not cost the user the beat
    // grid they read tempo from. The subdivision level is the snap division and only exists when it
    // is finer than a beat; visibleLineRange drops it when its lines would land under ~3 px apart.
    const double division = currentGridBeats(); // 0.0 == Snap::Off: no sub-beat level to draw
    if (division > 0.0 && division < 1.0)
        drawLevel(division, GridLineLevel::Subdivision);
    drawLevel(1.0, GridLineLevel::Beat);
    drawLevel(currentBeatsPerBar(), GridLineLevel::Bar);
}

void PianoRollComponent::paintGrid(juce::Graphics& g) {
    using namespace synth::theme;
    juce::Colour whiteRow, blackRow, rowSep, dimColour;
    if (auto* lf = dynamic_cast<AppLookAndFeel*>(&getLookAndFeel())) {
        const auto& c = lf->getTheme().colors;
        whiteRow = c.bg1;
        blackRow = c.surfaceHi;
        rowSep = c.border;
        dimColour = c.bg0;
    } else {
        whiteRow = juce::Colours::darkgrey.darker(0.3f);
        blackRow = juce::Colours::darkgrey.darker(0.6f);
        rowSep = juce::Colours::grey;
        dimColour = juce::Colours::black;
    }

    const int width = getWidth();
    const int height = getHeight();
    const int rowHeight = std::max(1, (int)pixelsPerSemitone_);
    const int visibleRows = std::max(0, (height - kHeaderHeight) / rowHeight) + 2;
    const long long totalRows = (long long)visiblePitches_.size();
    if (totalRows > 0) {
        const long long firstRow = (long long)nearestVisibleRowIndex(firstVisiblePitch_);
        for (int i = -1; i <= visibleRows; ++i) {
            const long long row = firstRow - i;
            if (row < 0 || row >= totalRows)
                continue;
            const int pitch = visiblePitches_[(size_t)row];
            const int y = yForPitch(pitch);
            g.setColour(isBlackKeyPitchClass(((pitch % 12) + 12) % 12) ? blackRow : whiteRow);
            g.fillRect(0, y, width, rowHeight);
            g.setColour(rowSep.withAlpha(0.35f));
            g.drawHorizontalLine(y, 0.0f, (float)width);
        }
    }

    // Vertical lines at the CURRENT snap division (lightest), beats (medium) and bars (strongest) —
    // pure state, redrawn whenever the snap selector changes because the panel repaints us then.
    // dimColour is bg0, the same fill paint() puts down behind everything, and is what the shared
    // policy contrasts the lines against.
    paintGridLines(g, rowSep, dimColour);

    const auto* clip = doc_->getClip(clipId_);
    if (clip == nullptr)
        return;

    // Dim the part of the grid outside [clipStart, clipEnd) — notes can only exist inside the
    // clip, so anything outside it is never editable.
    const float top = (float)kHeaderHeight;
    const float bottom = (float)height;
    const float startX = (float)beatToX(clip->startBeat);
    const float endX = (float)beatToX(clip->startBeat + clip->lengthBeats);
    g.setColour(dimColour.withAlpha(0.55f));
    if (startX > 0.0f)
        g.fillRect(juce::Rectangle<float>(0.0f, top, juce::jlimit(0.0f, (float)width, startX), bottom - top));
    if (endX < (float)width) {
        const float clampedEnd = juce::jlimit(0.0f, (float)width, endX);
        g.fillRect(juce::Rectangle<float>(clampedEnd, top, (float)width - clampedEnd, bottom - top));
    }

    for (const auto& note : clip->notes)
        paintNote(g, note);
}

void PianoRollComponent::paintNote(juce::Graphics& g, const synth::MidiNote& note) {
    const auto* clip = doc_->getClip(clipId_); // guaranteed non-null by paintGrid's caller
    const auto geom = effectiveGeometryFor(note);
    const auto rect = computeNoteRect(clip->startBeat + geom.startBeat, geom.lengthBeats, geom.pitch);
    if (rect.getRight() < 0 || rect.getX() > getWidth())
        return; // cheap offscreen cull, same reasoning as TimelineClipLaneArea::paintClip

    const bool selected = selection_.contains(note.id);
    // geom.pitch/geom.velocity (not note.pitch/note.velocity) so a mid-drag move or velocity scrub
    // is coloured by what is actually being previewed — including the out-of-scale colour, if the
    // drag has carried the note out of the active scale.
    const auto paint = resolveNoteColourFor(geom.pitch, geom.velocity, selected, note.muted);
    const auto bodyBounds = rect.toFloat().reduced(0.5f, 1.0f);

    // The muted dimming (desaturate + lower alpha, see NoteColour.h's kMutedNote* constants) is
    // baked into `paint` already — a muted note still gets exactly this same fill+outline paint,
    // just dimmed, so its selection halo survives the dimming the same way it always has.
    g.setColour(paint.fill);
    g.fillRoundedRectangle(bodyBounds, 2.0f);
    g.setColour(paint.border);
    g.drawRoundedRectangle(bodyBounds, 2.0f, selected ? 2.0f : 1.0f);
}

void PianoRollComponent::paintDrawPreview(juce::Graphics& g) {
    if (dragMode_ != DragMode::DrawNew || drawLengthBeats_ <= 0.0)
        return;
    const auto* clip = doc_->getClip(clipId_);
    if (clip == nullptr)
        return;

    juce::Colour accent = juce::Colours::cyan;
    if (auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel()))
        accent = lf->getTheme().colors.accent;

    const auto rect = computeNoteRect(clip->startBeat + drawStartBeat_, drawLengthBeats_, drawPitch_);
    const auto bounds = rect.toFloat().reduced(0.5f, 1.0f);
    g.setColour(accent.withAlpha(0.25f));
    g.fillRoundedRectangle(bounds, 2.0f);
    g.setColour(accent.withAlpha(0.8f));
    g.drawRoundedRectangle(bounds, 2.0f, 1.0f);
}

void PianoRollComponent::paintSplitPreview(juce::Graphics& g) {
    if (!hasSplitPreview_)
        return;
    const auto* clip = doc_->getClip(clipId_);
    const auto* note = doc_->getNote(splitPreviewNote_);
    if (clip == nullptr || note == nullptr)
        return;

    juce::Colour accent = juce::Colours::cyan;
    if (auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel()))
        accent = lf->getTheme().colors.accent;

    const float x = (float)beatToX(clip->startBeat + splitPreviewBeat_);
    const float y = (float)yForPitch(note->pitch);
    g.setColour(accent);
    g.fillRect(x - 0.5f, y, 1.0f, (float)std::max(1, (int)pixelsPerSemitone_));
}

void PianoRollComponent::paintPlayhead(juce::Graphics& g) {
    if (!hasPlayheadX_)
        return; // nothing has told us where the transport is yet

    const auto grid = gridRegion();
    const int x = getPlayheadLineX();
    if (grid.isEmpty() || x < grid.getX() || x > grid.getRight())
        return;

    juce::Colour accent = juce::Colours::cyan;
    if (auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel()))
        accent = lf->getTheme().colors.accent;

    g.setColour(accent);
    g.fillRect((float)x - kPlayheadLineWidth * 0.5f, (float)grid.getY(), kPlayheadLineWidth, (float)grid.getHeight());
}

void PianoRollComponent::paintKeysColumn(juce::Graphics& g) {
    using namespace synth::theme;
    juce::Colour whiteKey, blackKey, sep;
    float microSize = 8.5f;
    if (auto* lf = dynamic_cast<AppLookAndFeel*>(&getLookAndFeel())) {
        const auto& c = lf->getTheme().colors;
        whiteKey = c.pianoKeyWhite;
        blackKey = c.pianoKeyBlack;
        sep = c.border;
        microSize = lf->getTheme().type.micro;
    } else {
        whiteKey = juce::Colours::whitesmoke;
        blackKey = juce::Colours::black;
        sep = juce::Colours::grey;
    }

    // White fills the FULL column width first; every black-key row then overlays a narrower rect
    // flush left, leaving its own right portion showing the white fill underneath — the "gap
    // between black keys" a real keyboard shows when viewed side-on.
    g.setColour(whiteKey);
    g.fillRect(keysColumnBounds_);

    const int rowHeight = std::max(1, (int)pixelsPerSemitone_);
    const int visibleRows = std::max(0, keysColumnBounds_.getHeight() / rowHeight) + 2;
    const int blackWidth = blackKeyInsetForTest();
    const long long totalRows = (long long)visiblePitches_.size();
    if (totalRows == 0) {
        g.setColour(sep);
        g.drawVerticalLine(keysColumnBounds_.getRight() - 1, (float)keysColumnBounds_.getY(),
                           (float)keysColumnBounds_.getBottom());
        return;
    }
    const long long firstRow = (long long)nearestVisibleRowIndex(firstVisiblePitch_);

    for (int i = -1; i <= visibleRows; ++i) {
        const long long row = firstRow - i;
        if (row < 0 || row >= totalRows)
            continue;
        const int pitch = visiblePitches_[(size_t)row];
        const int pitchClass = ((pitch % 12) + 12) % 12;
        const int y = yForPitch(pitch);
        const juce::Rectangle<int> rowRect(keysColumnBounds_.getX(), y, keysColumnBounds_.getWidth(), rowHeight);
        const bool isBlack = isBlackKeyPitchClass(pitchClass);
        const juce::Colour keyFill = isBlack ? blackKey : whiteKey;
        if (isBlack) {
            g.setColour(blackKey);
            g.fillRect(rowRect.getX(), rowRect.getY(), blackWidth, rowRect.getHeight());
        }

        // A real keyboard's only FULL-width key separators are between the two white-key pairs
        // with no black key between them (E/F, B/C) — every other boundary gets a subtler hint,
        // since a black key already does most of the visual separating there.
        const bool fullWidthSep = (pitchClass == 4 || pitchClass == 11); // boundary above E, above B
        g.setColour(sep.withAlpha(fullWidthSep ? 0.5f : 0.15f));
        g.drawHorizontalLine(y, (float)rowRect.getX(), (float)rowRect.getRight());

        const auto label = keyLabelFor(pitch, keyLabelMode_, rowHeight);
        if (label.isNotEmpty()) {
            // .contrasting so the label holds against BOTH the white and the black key fill, in
            // every theme — never a fixed textCol that could sit invisible on one of the two.
            g.setColour(keyFill.contrasting(0.7f));
            g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), microSize, juce::Font::plain));
            g.drawText(label, rowRect.reduced(3, 0), juce::Justification::centredRight, false);
        }
    }

    g.setColour(sep);
    g.drawVerticalLine(keysColumnBounds_.getRight() - 1, (float)keysColumnBounds_.getY(),
                       (float)keysColumnBounds_.getBottom());
}

void PianoRollComponent::paintHeader(juce::Graphics& g) {
    using namespace synth::theme;
    juce::Colour bg, border, textCol, accent;
    float microSize = 8.5f;
    if (auto* lf = dynamic_cast<AppLookAndFeel*>(&getLookAndFeel())) {
        const auto& c = lf->getTheme().colors;
        bg = c.surface;
        border = c.border;
        textCol = c.textPrimary;
        accent = c.accent;
        microSize = lf->getTheme().type.micro;
    } else {
        bg = juce::Colours::darkslategrey;
        border = juce::Colours::grey;
        textCol = juce::Colours::white;
        accent = juce::Colours::cyan;
    }

    g.setColour(bg);
    g.fillRect(0, 0, getWidth(), kHeaderHeight);
    g.setColour(border);
    g.drawHorizontalLine(kHeaderHeight - 1, 0.0f, (float)getWidth());

    // Back button: a plain juce::Path triangle (never a Unicode glyph through a themed font) +
    // "Clips" text — the same "draw it, don't asset it" rule TimelineTransportBar's GlyphButton
    // follows for its own one-off shapes.
    auto arrowArea = backButtonBounds_.withWidth(9).reduced(0, 4);
    juce::Path arrow;
    arrow.addTriangle((float)arrowArea.getRight(), (float)arrowArea.getY(), (float)arrowArea.getRight(),
                      (float)arrowArea.getBottom(), (float)arrowArea.getX(), (float)arrowArea.getCentreY());
    g.setColour(textCol);
    g.fillPath(arrow);
    g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), microSize, juce::Font::plain));
    g.drawText("Clips", backButtonBounds_.withTrimmedLeft(12), juce::Justification::centredLeft, false);
    g.setColour(border);
    g.drawRoundedRectangle(backButtonBounds_.toFloat(), 3.0f, 1.0f);

    // Snap toggle ("Q"): lit while grid magnetism is on (snapEnabled AND a division to snap to),
    // muted while off — a real on/off switch, not an action button. The momentary flash (a fill
    // behind the glyph, ended by the one-shot timer in timerCallback()) still acknowledges every
    // press, including Shift+click's one-shot quantise.
    const bool snapOn = viewState_.snapEnabled && viewState_.snap != TimelineViewState::Snap::Off;
    if (snapOn) {
        g.setColour(accent.withAlpha(0.18f));
        g.fillRoundedRectangle(quantiseButtonBounds_.toFloat(), 3.0f);
    }
    if (quantiseFlash_) {
        g.setColour(accent.withAlpha(0.35f));
        g.fillRoundedRectangle(quantiseButtonBounds_.toFloat(), 3.0f);
    }
    g.setColour(snapOn ? accent : textCol.withAlpha(0.35f));
    g.drawText("Q", quantiseButtonBounds_, juce::Justification::centred, false);
    g.setColour(snapOn ? accent.withAlpha(0.8f) : border.withAlpha(0.5f));
    g.drawRoundedRectangle(quantiseButtonBounds_.toFloat(), 3.0f, 1.0f);

    // Scale button: same lit/muted treatment as "Q" above, but lit state tracks whether the panel
    // is OPEN rather than a grid-magnetism switch — an on/off affordance for a child component,
    // not for a doc mutation.
    const bool scaleOpen = scalePanel_.isVisible();
    if (scaleOpen) {
        g.setColour(accent.withAlpha(0.18f));
        g.fillRoundedRectangle(scaleButtonBounds_.toFloat(), 3.0f);
    }
    g.setColour(scaleOpen ? accent : textCol.withAlpha(0.85f));
    g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), microSize, juce::Font::plain));
    g.drawText("Scale", scaleButtonBounds_, juce::Justification::centred, false);
    g.setColour(scaleOpen ? accent.withAlpha(0.8f) : border.withAlpha(0.5f));
    g.drawRoundedRectangle(scaleButtonBounds_.toFloat(), 3.0f, 1.0f);
}

void PianoRollComponent::paintMarquee(juce::Graphics& g) {
    if (marqueeRect_.isEmpty())
        return;

    // SAME token recipe as GraphEditor's module-marquee band (GraphEditor.cpp
    // GraphEditor::Content::paint, "Marquee selection band", issue #156) and
    // TimelineClipLaneArea::paintMarquee: accent fill at low alpha + a brighter accent border at
    // the theme's guideLineWidth. Previously a flat white at low alpha, easy to lose against a
    // light theme's background; this keeps all three marquees moving together on a theme change.
    auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());
    const juce::Colour accentColour = lf != nullptr ? lf->getTheme().colors.accent : juce::Colours::cyan;
    const float lineWidth = lf != nullptr ? lf->getTheme().metrics.guideLineWidth : 1.5f;

    const auto bandF = marqueeRect_.toFloat();
    g.setColour(accentColour.withAlpha(0.12f));
    g.fillRect(bandF);
    g.setColour(accentColour.withAlpha(0.80f));
    g.drawRect(bandF, lineWidth);
}

//==============================================================================
// ---- Local playhead (driven by TimelinePlayheadOverlay — no timer here) ----

int PianoRollComponent::getPlayheadLineX() const noexcept {
    // Guard rail identical to the overlay's: llround of an unbounded double is not well-behaved,
    // and nothing beyond a few screen widths can ever be visible.
    constexpr double kMaxLineXMagnitude = 1.0e7;
    return (int)std::llround(std::clamp(beatToX(playheadBeat_), -kMaxLineXMagnitude, kMaxLineXMagnitude));
}

juce::Rectangle<int> PianoRollComponent::playheadStripFor(int x) const noexcept {
    return {x - kPlayheadStripHalfWidth, 0, 2 * kPlayheadStripHalfWidth + 1, getHeight()};
}

void PianoRollComponent::requestRepaintStrip(juce::Rectangle<int> strip) { repaint(strip); }

void PianoRollComponent::requestRepaintPreviewStrip(juce::Rectangle<int> strip) { repaint(strip); }

void PianoRollComponent::setPlayheadBeat(double absoluteBeat) {
    if (!std::isfinite(absoluteBeat))
        return;
    playheadBeat_ = absoluteBeat;

    // Follow: page-flip the roll's OWN view BEFORE the strip diff below runs, so that diff is
    // computed against the (possibly just-moved) mapping the line will actually draw at. Gated on
    // no drag being in flight and the edge-auto-scroll timer being idle — either one is already
    // steering rollView_ on its own terms, and a follow flip landing on top of it would fight the
    // gesture the user is mid-way through. The check costs nothing while the beat is already
    // inside the view (the common, playing-and-visible case): it returns without ever calling
    // setHorizontalView, so the zero-repaint-while-unmoved contract below is untouched — follow
    // adds no work at all until the line is about to leave the visible grid.
    if (followPlayhead_ && dragMode_ == DragMode::None && !autoScrollTimer_.isTimerRunning()) {
        const auto grid = gridRegion();
        if (!grid.isEmpty() && rollView_.pixelsPerBeat > 0.0) {
            const int rawX = (int)std::llround(beatToX(absoluteBeat));
            if (rawX < grid.getX() || rawX > grid.getRight()) {
                // Land the beat a TENTH of the visible span past the gutter rather than flush
                // against it — a beat sitting exactly on the edge would immediately re-trigger the
                // same flip on the very next tick once it advances one more sample.
                const double visibleBeats = (double)grid.getWidth() / rollView_.pixelsPerBeat;
                setHorizontalView(rollView_.pixelsPerBeat, std::max(0.0, absoluteBeat - 0.1 * visibleBeats));
            }
        }
    }

    const int x = getPlayheadLineX();
    // The FIRST beat after an open is a real change (there is no line on screen yet), so it costs
    // exactly one strip. Every later beat that lands on the same pixel costs nothing — that is what
    // keeps a stopped transport at zero repaints.
    const int previousX = hasPlayheadX_ ? playheadLineX_ : x;
    if (hasPlayheadX_ && x == playheadLineX_)
        return;
    hasPlayheadX_ = true;
    playheadLineX_ = x;

    const auto strip = playheadStripFor(previousX).getUnion(playheadStripFor(x)).getIntersection(gridRegion());
    if (!strip.isEmpty())
        requestRepaintStrip(strip);
}

//==============================================================================
void PianoRollComponent::resized() {
    auto bounds = getLocalBounds();
    auto header = bounds.removeFromTop(kHeaderHeight);
    backButtonBounds_ = header.removeFromLeft(60).reduced(3, 2);
    header.removeFromLeft(4);
    quantiseButtonBounds_ = header.removeFromLeft(20).reduced(2, 2);
    header.removeFromLeft(4);
    scaleButtonBounds_ = header.removeFromLeft(50).reduced(2, 2);

    // The panel sits WEST of the keys column, below the header row (its own controls, not this
    // header strip's buttons, are how the user works it) — carved BEFORE the keys column so
    // leftGutterWidth() and this layout can never disagree about where the grid starts.
    if (scalePanel_.isVisible())
        scalePanel_.setBounds(bounds.removeFromLeft(kScalePanelWidth));
    else
        scalePanel_.setBounds({});

    keysColumnBounds_ = bounds.removeFromLeft(kKeysColumnWidth);
    noteGridBounds_ = bounds; // a REAL gutter: beatToX(firstVisibleBeat) == this rect's left edge
}

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
    previewDeltaBeats_ = 0.0;
    previewDeltaPitch_ = 0;
    previewDeltaVelocity_ = 0;
    drawLengthBeats_ = 0.0;
    clearSplitPreview();
    autoScrollTimer_.stopTimer(); // the auto-scroll timer's drag just got cancelled too

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

    if (activeTool_ == EditTool::Split && doc_ != nullptr && clipId_.isValid() && pos.y >= kHeaderHeight &&
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
    if (doc_ != nullptr && clipId_.isValid() && pos.y >= kHeaderHeight && pos.x >= leftGutterWidth()) {
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

//==============================================================================
// ---- Tooltips ----

juce::String PianoRollComponent::getTooltipFor(juce::Point<int> pos) const {
    if (quantiseButtonBounds_.contains(pos))
        return juce::String(kQuantiseTooltip);
    if (scaleButtonBounds_.contains(pos))
        return juce::String(kScaleTooltip);
    return {};
}

juce::String PianoRollComponent::getTooltip() { return getTooltipFor(getMouseXYRelative()); }

//==============================================================================
// ---- Mouse ----

void PianoRollComponent::mouseDown(const juce::MouseEvent& e) {
    grabKeyboardFocus();
    dragMode_ = DragMode::None;
    pendingEmptyClick_ = false;

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
    if (quantiseButtonBounds_.contains(pos)) {
        flashQuantiseButton(); // feedback even when the click is a no-op
        if (e.mods.isShiftDown())
            performQuantise(); // one-shot: snap the existing notes to the grid
        else
            toggleSnap(); // plain click: the grid-magnetism switch
        return;
    }
    if (scaleButtonBounds_.contains(pos)) {
        toggleScalePanel();
        return;
    }
    if (pos.y < kHeaderHeight)
        return; // rest of the header strip: inert
    if (pos.x < leftGutterWidth())
        return; // keys column (and scale panel, while open): no virtual-keyboard preview in v1

    // Everything below this line is the SELECT tool's gesture table. The other five tools act on
    // the click alone and start no drag at all (see setActiveTool), so they never reach it — no
    // modifier combination can turn an Erase click into a move.
    if (activeTool_ != EditTool::Select) {
        handleToolMouseDown(pos);
        return;
    }

    auto hit = hitTestNote(pos);

    if (hit && e.mods.isCommandDown() && !e.mods.isShiftDown()) {
        // Cmd is ADDITIVE on a note (never a toggle — the drag that may follow scrubs the whole
        // selection's velocity, and yanking the grabbed note out of it mid-gesture is never wanted).
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
        const double snappedAnchorStart = snappedBeatAt(anchorOriginalStart + deltaBeatsRaw);
        double delta = snappedAnchorStart - anchorOriginalStart;

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
    } else if (dragMode_ == DragMode::Resize) {
        const auto* note = doc_->getNote(activeNote_);
        if (note == nullptr)
            return;
        const double grid = currentGridBeats();
        const double minLen = grid > 0.0 ? grid : kMinNoteLengthBeats;
        const double rawEnd = xToBeat((double)pos.x) - clip->startBeat;
        const double snappedEnd = snappedBeatAt(rawEnd);
        double length = std::max(snappedEnd - note->startBeat, minLen);
        length = std::min(length, std::max(0.0, clip->lengthBeats - note->startBeat));
        previewLength_ = length;
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
        // pitch increases upward — which means firstVisiblePitch_ (the pitch of the TOP row) must
        // INCREASE, the opposite sign from the raw velocity.
        const long long total = (long long)visiblePitches_.size();
        const long long currentRow = (long long)nearestVisibleRowIndex(firstVisiblePitch_);
        const long long rowStep = -(long long)std::llround(vVelocityRowsPerTick);
        const long long newRow = std::clamp(currentRow + rowStep, 0LL, total - 1);
        firstVisiblePitch_ = visiblePitches_[(size_t)newRow];
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
        const auto id = activeNote_;
        const double newLength = previewLength_;
        auto mutate = [this, id, newLength] { doc_->resizeNote(id, newLength); };
        if (undoManager_)
            undoManager_->recordTimelineChange(*doc_, mutate);
        else
            mutate();
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
    previewDeltaBeats_ = 0.0;
    previewDeltaPitch_ = 0;
    previewDeltaVelocity_ = 0;
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
    if (pos.y < kHeaderHeight || pos.x < leftGutterWidth())
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

void PianoRollComponent::mouseMove(const juce::MouseEvent& e) {
    const auto pos = e.getPosition();
    updateSplitPreview(pos);
    updateHoverCursor(pos);
}

void PianoRollComponent::mouseEnter(const juce::MouseEvent& e) {
    // The cursor is set on ENTER (and on a tool change), never per move — six rasterised icons is
    // not per-frame work.
    applyToolCursor();
    updateSplitPreview(e.getPosition());
    updateHoverCursor(e.getPosition());
}

void PianoRollComponent::mouseExit(const juce::MouseEvent&) { clearSplitPreview(); }

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
        const int deltaRows = (int)std::llround(-amountY * kPitchScrollSemitonesPerWheelUnit);
        // Walks visiblePitches_ by ROW INDEX, not by raw semitone — with pitch-visibility collapsing
        // out-of-scale rows, "scroll one wheel-unit's worth of rows" has to mean the same number of
        // ROWS whatever their semitone spacing happens to be (see the class comment).
        if (!visiblePitches_.empty()) {
            const long long total = (long long)visiblePitches_.size();
            const long long currentRow = (long long)nearestVisibleRowIndex(firstVisiblePitch_);
            const long long nextRow = std::clamp(currentRow + (long long)deltaRows, 0LL, total - 1);
            // Gated on the CLAMPED result, not on the delta: a wheel that keeps pushing past the top
            // row (or a gesture too small to round to a whole row) must cost zero repaints.
            const int next = visiblePitches_[(size_t)nextRow];
            if (next != firstVisiblePitch_) {
                firstVisiblePitch_ = next;
                repaint();
            }
        }
    }
}

//==============================================================================
// ---- Anchored zoom (one implementation; the wheel, the pinch and the public API share it) ----

void PianoRollComponent::zoomHorizontalAroundX(double factor, double anchorGridX) {
    if (!std::isfinite(factor) || factor <= 0.0)
        return;
    rollView_.zoomAroundX(factor, anchorGridX);
    repaint();
    if (onHorizontalViewChanged)
        onHorizontalViewChanged();
}

void PianoRollComponent::zoomVerticalAroundY(double factor, double anchorY) {
    if (!std::isfinite(factor) || factor <= 0.0 || !std::isfinite(anchorY) || visiblePitches_.empty())
        return;
    // The anchor is held in ROW-INDEX space, not raw pitch, for the same reason the wheel-scroll
    // branch walks visiblePitches_ by index: with rows collapsed, "row under the cursor" and
    // "pitch under the cursor" can disagree, and the row is what visually sits at anchorY.
    // firstVisiblePitch_ is an int (so is a row index), so the anchor holds to within one row —
    // enough for a wheel gesture, a pinch, or a zoom command.
    const double firstRow = (double)nearestVisibleRowIndex(firstVisiblePitch_);
    const double rowsAbove = (anchorY - (double)kHeaderHeight) / pixelsPerSemitone_;
    const double anchorRow = firstRow - rowsAbove;
    setPixelsPerSemitone(pixelsPerSemitone_ * factor);
    const double newRowsAbove = (anchorY - (double)kHeaderHeight) / pixelsPerSemitone_;
    const long long total = (long long)visiblePitches_.size();
    const long long newRow = std::clamp((long long)std::llround(anchorRow + newRowsAbove), 0LL, total - 1);
    firstVisiblePitch_ = visiblePitches_[(size_t)newRow];
    repaint();
}

double PianoRollComponent::wheelZoomFactor(const juce::MouseWheelDetails& wheel) const noexcept {
    // wheelGestureIsUpward already recovers the PHYSICAL direction from isReversed XOR the delta's
    // sign (see ScrollPolicy.h) — "up" here means the same finger motion regardless of the OS's
    // natural-scrolling setting. zoomScrollInverted_ is the app-level preference stacked on top,
    // independent of scrollInverted_ (that one only ever governs the plain-scroll branches, which
    // deliberately keep following the OS convention instead — see mouseWheelMove).
    const bool zoomIn = wheelGestureIsUpward(wheel) != zoomScrollInverted_;
    // Magnitude only — the sign now comes entirely from zoomIn above, not from dominantWheelDelta's
    // own sign, otherwise isReversed (folded into the delta already) would double-count direction.
    const double magnitude = std::abs(dominantWheelDelta(wheel)) * kZoomWheelSensitivity;
    return std::exp(zoomIn ? magnitude : -magnitude);
}

void PianoRollComponent::zoomHorizontal(double factor) {
    // The view centre, expressed in the same grid-relative coordinate the Cmd+wheel branch hands
    // over (x - leftGutterWidth()), so both paths run identical anchor math.
    zoomHorizontalAroundX(factor, (double)gridRegion().getWidth() * 0.5);
}

void PianoRollComponent::zoomVertical(double factor) {
    const auto grid = gridRegion();
    zoomVerticalAroundY(factor, (double)grid.getY() + (double)grid.getHeight() * 0.5);
}

void PianoRollComponent::mouseMagnify(const juce::MouseEvent& e, float scaleFactor) {
    // Trackpad pinch, same pair as the panel's: plain = horizontal zoom around the pinch point
    // (through the roll's OWN mapping), Shift = vertical (pixels-per-semitone) zoom.
    // A pinch carries no wheel deltas at all, so there is no axis to disambiguate here — it goes
    // straight to the same anchored-zoom helpers the Cmd+wheel branches use.
    if (!std::isfinite(scaleFactor) || scaleFactor <= 0.0f)
        return;
    const auto pos = e.getPosition();
    if (e.mods.isShiftDown()) {
        zoomVerticalAroundY((double)scaleFactor, (double)pos.y);
        return;
    }
    zoomHorizontalAroundX((double)scaleFactor, std::max(0.0, (double)pos.x - (double)leftGutterWidth()));
}

//==============================================================================
bool PianoRollComponent::matchesAction(const juce::KeyPress& key, const juce::String& actionId,
                                       const juce::KeyPress& fallback) const {
    if (shortcuts_ == nullptr)
        return key == fallback;
    const auto binding = shortcuts_->getBinding(actionId);
    // An invalid binding is "this action has no key": either the user cleared it, or this build's
    // ShortcutManager has never heard of the id (getBinding answers an unknown id with a
    // default-constructed KeyPress). Falling back to `fallback` here would resurrect a key the user
    // deliberately unbound, so it deliberately does not.
    //
    // Routed through ShortcutManager::keyPressMatches rather than raw juce::KeyPress::operator==:
    // macOS delivers a Shift-chorded symbol key as the SHIFTED CHARACTER ('!' not '1', '+' not '='),
    // never the base key plus a Shift modifier flag, so exact equality would silently never match a
    // user rebind onto such a chord — keyPressMatches shift-normalizes exactly that case.
    return binding.isValid() && ShortcutManager::keyPressMatches(binding, key);
}

bool PianoRollComponent::keyPressed(const juce::KeyPress& key) {
    // Shift+Q = one-shot quantise; Q = toggle grid magnetism (same pair as the header button's
    // Shift+click / click). Handled here so both work while the roll has focus; the panel handles
    // the same keys for every other focus target inside the timeline.
    //
    // Quantise is tested FIRST because it is the more specific of the two: with the defaults the
    // snap toggle is a bare Q and quantise is Shift+Q, and a user is free to rebind them into any
    // other pair. The snap toggle shares "timelineSnapToggle" with the panel — one binding, one key,
    // whichever surface has focus.
    if (matchesAction(key, "pianoRollQuantise", modKey('q', juce::ModifierKeys::shiftModifier))) {
        flashQuantiseButton();
        performQuantise();
        return true;
    }
    if (matchesAction(key, "timelineSnapToggle", plainKey('q'))) {
        flashQuantiseButton();
        toggleSnap();
        return true;
    }

    // Escape and Delete/Backspace are FIXED, never manager-resolved: "cancel" and "delete the
    // selection" are platform conventions every surface in the app answers identically, not app
    // shortcuts a user would expect to find in a rebinding list.
    if (key == juce::KeyPress::escapeKey) {
        if (!selection_.isEmpty()) {
            selection_.clear();
            repaint();
            return true;
        }
        requestClose();
        return true;
    }

    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey) {
        auto ids = selection_.getSelected();
        if (ids.empty() || doc_ == nullptr)
            return false;

        auto mutate = [this, ids] {
            for (auto id : ids)
                doc_->removeNote(id);
        };
        if (undoManager_)
            undoManager_->recordTimelineChange(*doc_, mutate);
        else
            mutate();

        selection_.clear();
        repaint();
        return true;
    }

    // The rest of the surface's keys act on the SELECTION and nothing else: with nothing selected
    // they fall through (return false), so the panel — and the graph behind it — keep whatever those
    // keys mean there. Nudge/transpose EDIT the selected notes; the two navigation actions only MOVE
    // the selection between notes.
    //
    // Each one is resolved through matchesAction, so the defaults listed below are exactly that:
    // defaults. The order matters only where one default is a modified form of another (Shift+Up vs
    // Up, Alt+Left vs Left) — the more specific action is tested first so a user who rebinds only
    // one of a pair cannot end up with the other swallowing it. juce::KeyPress equality is exact on
    // modifiers, which is what keeps Left, Shift+Left and Alt+Left three separate actions.
    //
    // Alt+Up/Down stays RESERVED: no action claims it, so it falls through for whatever the vertical
    // half of navigation turns out to be. Digit keys are deliberately absent too — tool switching
    // belongs to the panel (see setActiveTool).
    if (matchesAction(key, "pianoRollNavNextNote", modKey(juce::KeyPress::rightKey, juce::ModifierKeys::altModifier)))
        return selectAdjacentNote(true);
    if (matchesAction(key, "pianoRollNavPrevNote", modKey(juce::KeyPress::leftKey, juce::ModifierKeys::altModifier)))
        return selectAdjacentNote(false);

    if (matchesAction(key, "pianoRollNudgeRight", plainKey(juce::KeyPress::rightKey)))
        return nudgeSelectedNotes(1);
    if (matchesAction(key, "pianoRollNudgeLeft", plainKey(juce::KeyPress::leftKey)))
        return nudgeSelectedNotes(-1);

    // Shift is the octave jump, the same 12-semitone convention every DAW uses — a separate action
    // rather than a modifier read off the plain one, so it can be rebound on its own.
    if (matchesAction(key, "pianoRollTransposeOctaveUp",
                      modKey(juce::KeyPress::upKey, juce::ModifierKeys::shiftModifier)))
        return transposeSelectedNotes(12);
    if (matchesAction(key, "pianoRollTransposeOctaveDown",
                      modKey(juce::KeyPress::downKey, juce::ModifierKeys::shiftModifier)))
        return transposeSelectedNotes(-12);
    if (matchesAction(key, "pianoRollTransposeUp", plainKey(juce::KeyPress::upKey)))
        return transposeSelectedNotes(1);
    if (matchesAction(key, "pianoRollTransposeDown", plainKey(juce::KeyPress::downKey)))
        return transposeSelectedNotes(-1);

    return false;
}

} // namespace synth::ui
