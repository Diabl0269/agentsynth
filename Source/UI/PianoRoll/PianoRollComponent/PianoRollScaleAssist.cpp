// PianoRollComponent — the scale-assist panel: opening/closing its slide-in animation, pushing the
// active MusicalScale context down into the roll's visible-pitch filtering, and the panel's
// Generate-random-notes action. The class itself is declared in PianoRollComponent.h; sibling
// PianoRoll<Concern>.cpp units in this directory hold the rest (construction/geometry, painting,
// edit tools, audition, clipboard, mouse, zoom).

#include "PianoRollComponent.h"

#include "AppUndoManager.h"
#include "Transport/TransportService.h"
#include "UI/Theme/AppLookAndFeel.h"
#include <algorithm>
#include <cmath>

namespace synth::ui {

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
    // A restore must never itself look like the panel sliding open — see the class comment.
    setScalePanelVisible(visible, /*animate=*/false);
}

void PianoRollComponent::setScalePanelVisible(bool visible, bool animate) {
    if (scalePanelVisible_ == visible)
        return; // restoring the same state (a fresh PropertiesFile's default, or a repeat toggle) costs nothing
    scalePanelVisible_ = visible;
    if (propertiesFile_ != nullptr) {
        propertiesFile_->setValue(kScalePanelVisiblePropertyKey, visible);
        propertiesFile_->saveIfNeeded();
    }

    // The tween starts from WHEREVER the slide is right now — a mid-flight toggle reverses from
    // there rather than jumping to an extreme first (see getScalePanelAnimFromForTest).
    scalePanelAnimFrom_ = scalePanelOpenProgress_;
    scalePanelAnimTo_ = visible ? 1.0f : 0.0f;
    if (visible)
        scalePanel_.setVisible(true); // shown for the WHOLE open-or-close slide; hidden again only
                                      // once a CLOSE finishes (finishScalePanelAnimation)

    if (!animate || !isShowing()) {
        // No VBlank reaches an off-screen component (headless tests, or a restore before the
        // window exists), and a persisted restore must never play a slide at all (see the class
        // comment on setPropertiesFile) — either way, land on the target immediately.
        finishScalePanelAnimation();
        return;
    }

    if (!scalePanelVblankUpdater_.has_value())
        scalePanelVblankUpdater_.emplace(this);
    const float from = scalePanelAnimFrom_;
    const float to = scalePanelAnimTo_;
    scalePanelAnim_.start(
        *scalePanelVblankUpdater_, kScalePanelAnimMs, synth::ui::easeInOutCubic,
        [this, from, to](float t) { applyScalePanelOpenProgress(from + (to - from) * t); },
        [this] { finishScalePanelAnimation(); });
}

void PianoRollComponent::applyScalePanelOpenProgress(float progress) {
    scalePanelOpenProgress_ = juce::jlimit(0.0f, 1.0f, progress);
    resized(); // leftGutterWidth() just moved — re-carve the panel/keys-column/grid split again
    repaint();
    // leftGutterWidth() genuinely moves EVERY frame of the slide, and that IS the roll's horizontal
    // mapping (beatToX/xToBeat offset by it) — the same seam a wheel zoom/scroll already fires.
    // TimelinePanelComponent's wiring re-issues the ruler's mapping-override offset from this, so
    // the ruler's ticks/hit-testing track the slide rather than only its two endpoints.
    if (onHorizontalViewChanged)
        onHorizontalViewChanged();
}

void PianoRollComponent::setScalePanelOpenProgressForTest(float progress) { applyScalePanelOpenProgress(progress); }

void PianoRollComponent::finishScalePanelAnimation() {
    if (scalePanelVblankUpdater_.has_value())
        scalePanelAnim_.stop(*scalePanelVblankUpdater_);
    applyScalePanelOpenProgress(scalePanelAnimTo_); // pin the EXACT end value
    if (!scalePanelVisible_)
        scalePanel_.setVisible(false); // closed at rest — gone only once the slide is actually done
}

void PianoRollComponent::toggleScalePanel() { setScalePanelVisible(!scalePanelVisible_); }

std::optional<synth::MusicalScale> PianoRollComponent::activeScaleForOpenClip() const {
    if (!clipId_.isValid())
        return std::nullopt;
    const auto it = clipScaleMemory_.find(clipId_);
    if (it == clipScaleMemory_.end())
        return std::nullopt; // a clip never opened before starts at "No scale" without inserting
    return it->second.scale;
}

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

bool PianoRollComponent::quantisePitchesToActiveScale() {
    if (!isPitchQuantiseEnabled())
        return false;
    const auto scale = activeScaleForOpenClip();
    if (!scale.has_value())
        return false;
    // By value (see activeScaleForOpenClip): quantisePitchesToScale mutates the doc, so the scale it
    // is handed must not be a reference into clipScaleMemory_.
    quantisePitchesToScale(*scale);
    return true;
}

bool PianoRollComponent::isPitchQuantiseEnabled() const {
    if (doc_ == nullptr || !clipId_.isValid())
        return false;
    if (!activeScaleForOpenClip().has_value())
        return false; // "No scale" has nothing to quantise INTO — a no-op, not an identity pass
    const auto* clip = doc_->getClip(clipId_);
    return clip != nullptr && !clip->notes.empty();
}

void PianoRollComponent::generateRandomNotesIntoClip(const synth::MusicalScale* scale, int minPitch, int maxPitch,
                                                     juce::Random& rng, bool addToExisting) {
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

    auto notes = synth::generateRandomNotes(clip->lengthBeats, gridBeats, minPitch, maxPitch, scale, rng);

    if (addToExisting) {
        // OVERLAY. Duplicate suppression is by exact (pitch, startBeat), which is precisely the key
        // a re-run collides on: generation walks the same grid steps every time, so without this a
        // second Generate would stack unison notes at the same offsets — invisible in the roll (one
        // rect drawn over another) and unclickable apart. A note at the same start but a DIFFERENT
        // pitch is a chord and is kept; different lengths at the same (pitch, start) are still the
        // same note as far as the user can tell, so the incoming one is dropped rather than merged.
        const auto isDuplicate = [clip](const synth::MidiNote& candidate) {
            for (const auto& existing : clip->notes)
                if (existing.pitch == candidate.pitch && std::abs(existing.startBeat - candidate.startBeat) < 1.0e-9)
                    return true;
            return false;
        };
        notes.erase(std::remove_if(notes.begin(), notes.end(), isDuplicate), notes.end());
    }

    // ONE mutation either way — the difference is only whether it starts by clearing. Replace means
    // undo restores the old contents in a single step rather than unwinding a clear-then-paste; add
    // means the existing notes were never touched, so undo just removes what was added.
    const bool clearFirst = !addToExisting;
    std::vector<synth::NoteId> newIds;
    auto mutate = [this, notes, clearFirst, &newIds] {
        if (clearFirst)
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

    // Only the notes that were actually ADDED — which is what makes "generate again" reviewable: the
    // selection is the diff, not the whole clip.
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
    return {gutter, canvasTop(), std::max(0, getWidth() - gutter), std::max(0, getHeight() - canvasTop())};
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

double PianoRollComponent::drawnGridBeats() const { return viewState_.divisionBeatsRaw(currentBeatsPerBar()); }

double PianoRollComponent::snappedBeatAt(double rawBeat) const {
    return viewState_.snapBeat(rawBeat, currentBeatsPerBar());
}

double PianoRollComponent::resizePreviewLengthFor(const NoteOrigin& origin) const noexcept {
    // The grabbed note takes the pointer's own value verbatim: snapping, the grid floor and the Cmd
    // bypass have all already been applied to it, and re-deriving it here from the delta could
    // disagree by a rounding step with what the mouse is on.
    if (origin.id == activeNote_)
        return previewLength_;
    // Everyone else: their OWN original length plus the shared delta, floored so a shortening drag
    // can never invert a note. The floor is the editor's absolute minimum rather than the grid — a
    // note already shorter than one division must not get LONGER because the group was trimmed.
    return std::max(origin.lengthBeats + previewLengthDelta_, kMinNoteLengthBeats);
}

PianoRollComponent::NoteGeometry PianoRollComponent::effectiveGeometryFor(const synth::MidiNote& note) const {
    if (dragMode_ == DragMode::Move) {
        for (const auto& origin : dragNotes_)
            if (origin.id == note.id)
                return {origin.startBeat + previewDeltaBeats_, origin.lengthBeats,
                        rowShiftedPitch(origin.pitch, previewDeltaPitch_), origin.velocity};
    } else if (dragMode_ == DragMode::Resize) {
        // Every note the gesture snapshotted, not just the grabbed one — a resize inside a
        // multi-selection previews the whole group by the shared delta (see resizeNotes_).
        for (const auto& origin : resizeNotes_)
            if (origin.id == note.id)
                return {note.startBeat, resizePreviewLengthFor(origin), note.pitch, note.velocity};
    } else if (dragMode_ == DragMode::VelocityScrub) {
        for (const auto& origin : dragNotes_)
            if (origin.id == note.id)
                return {note.startBeat, note.lengthBeats, note.pitch,
                        juce::jlimit(1, 127, origin.velocity + previewDeltaVelocity_)};
    }
    return {note.startBeat, note.lengthBeats, note.pitch, note.velocity};
}

} // namespace synth::ui
