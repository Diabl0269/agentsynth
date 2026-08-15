#include "PianoRollComponent.h"
#include "../AppUndoManager.h"
#include "../Transport/TransportService.h"
#include "Theme/AppLookAndFeel.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <set>

namespace synth::ui {

namespace {
// Wheel tuning for this component's OWN non-Cmd behaviour (deltaX -> time scroll, deltaY -> pitch
// scroll). kScrollPixelsPerWheelUnit intentionally mirrors TimelinePanelComponent::mouseWheelMove's
// constant of the same value rather than sharing it (that one is a private anonymous-namespace
// constant in a different translation unit) — the same one-line-cosmetic-constant duplication
// TimelinePanelComponent.cpp's own kMinBeatLinePixelsPerBeat comment already accepts. Cmd+wheel
// does NOT get a constant here: it is forwarded verbatim to the panel's zoom handling (see
// mouseWheelMove below), so the actual zoom math is never duplicated.
constexpr double kScrollPixelsPerWheelUnit = 200.0;
constexpr double kPitchScrollSemitonesPerWheelUnit = 3.0;

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
}

//==============================================================================
// ---- Entry/exit ----

void PianoRollComponent::openClip(synth::ClipId id) {
    dragMode_ = DragMode::None;
    selection_.clear();

    if (doc_ == nullptr) {
        clipId_ = {};
        return;
    }
    const auto* clip = doc_->getClip(id);
    if (clip == nullptr) {
        clipId_ = {};
        return;
    }
    clipId_ = id;

    const int visibleRows = std::max(1, (getHeight() - kHeaderHeight) / (int)kPixelsPerSemitone);
    const int median = medianPitchOf(clip->notes);
    firstVisiblePitch_ = juce::jlimit(0, 127, median + visibleRows / 2);
    repaint();
}

void PianoRollComponent::closeRoll() {
    clipId_ = {};
    selection_.clear();
    dragMode_ = DragMode::None;
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
        requestClose(); // the edited clip is gone — the roll closes itself (TL5-8 design)
        return;
    }

    std::vector<synth::NoteId> alive;
    alive.reserve(clip->notes.size());
    for (const auto& note : clip->notes)
        alive.push_back(note.id);
    selection_.retainOnly(alive);
    repaint();
}

//==============================================================================
// ---- Geometry ----

int PianoRollComponent::yForPitch(int pitch) const noexcept {
    return kHeaderHeight + (int)std::llround((double)(firstVisiblePitch_ - pitch) * kPixelsPerSemitone);
}

int PianoRollComponent::pitchForY(int y) const noexcept {
    return firstVisiblePitch_ - (int)std::floor((double)(y - kHeaderHeight) / kPixelsPerSemitone);
}

juce::Rectangle<int> PianoRollComponent::computeNoteRect(double absStartBeat, double absLengthBeats, int pitch) const {
    const double x0 = viewState_.beatToX(absStartBeat);
    const double x1 = viewState_.beatToX(absStartBeat + absLengthBeats);
    const int left = (int)std::llround(x0);
    const int right = (int)std::llround(x1);
    return {left, yForPitch(pitch), std::max(right - left, 1), (int)kPixelsPerSemitone};
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
                        juce::jlimit(0, 127, origin.pitch + previewDeltaPitch_), origin.velocity};
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
    paintKeysColumn(g);
    paintHeader(g);

    if (dragMode_ == DragMode::Marquee)
        paintMarquee(g);
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
    const int visibleRows = std::max(0, (height - kHeaderHeight) / (int)kPixelsPerSemitone) + 2;
    for (int i = -1; i <= visibleRows; ++i) {
        const int pitch = firstVisiblePitch_ - i;
        if (pitch < 0 || pitch > 127)
            continue;
        const int y = yForPitch(pitch);
        g.setColour(isBlackKeyPitchClass(pitch % 12) ? blackRow : whiteRow);
        g.fillRect(0, y, width, (int)kPixelsPerSemitone);
        g.setColour(rowSep.withAlpha(0.35f));
        g.drawHorizontalLine(y, 0.0f, (float)width);
    }

    const auto* clip = doc_->getClip(clipId_);
    if (clip == nullptr)
        return;

    // Dim the part of the grid outside [clipStart, clipEnd) — notes can only exist inside the
    // clip (TL5-8 design), so anything outside it is never editable.
    const float top = (float)kHeaderHeight;
    const float bottom = (float)height;
    const float startX = (float)viewState_.beatToX(clip->startBeat);
    const float endX = (float)viewState_.beatToX(clip->startBeat + clip->lengthBeats);
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

    using namespace synth::theme;
    juce::Colour accent, noteColour;
    if (auto* lf = dynamic_cast<AppLookAndFeel*>(&getLookAndFeel())) {
        accent = lf->getTheme().colors.accent;
        noteColour = lf->getTheme().colors.midiWire;
    } else {
        accent = juce::Colours::cyan;
        noteColour = juce::Colours::violet;
    }

    const bool selected = selection_.contains(note.id);
    const float brightness = juce::jlimit(0.2f, 1.0f, (float)geom.velocity / 127.0f);
    const juce::Colour fill =
        noteColour.withMultipliedBrightness(0.5f + 0.7f * brightness).withAlpha(selected ? 0.95f : 0.8f);

    const auto bodyBounds = rect.toFloat().reduced(0.5f, 1.0f);
    g.setColour(fill);
    g.fillRoundedRectangle(bodyBounds, 2.0f);
    g.setColour(selected ? accent : noteColour.darker(0.5f));
    g.drawRoundedRectangle(bodyBounds, 2.0f, selected ? 2.0f : 1.0f);
}

void PianoRollComponent::paintKeysColumn(juce::Graphics& g) {
    using namespace synth::theme;
    juce::Colour whiteKey, blackKey, sep, textCol;
    float microSize = 8.5f;
    if (auto* lf = dynamic_cast<AppLookAndFeel*>(&getLookAndFeel())) {
        const auto& c = lf->getTheme().colors;
        whiteKey = c.bg1;
        blackKey = c.surfaceHi;
        sep = c.border;
        textCol = c.textMuted;
        microSize = lf->getTheme().type.micro;
    } else {
        whiteKey = juce::Colours::darkgrey;
        blackKey = juce::Colours::black;
        sep = juce::Colours::grey;
        textCol = juce::Colours::lightgrey;
    }

    g.setColour(whiteKey);
    g.fillRect(keysColumnBounds_);

    const int visibleRows = std::max(0, keysColumnBounds_.getHeight() / (int)kPixelsPerSemitone) + 2;
    for (int i = -1; i <= visibleRows; ++i) {
        const int pitch = firstVisiblePitch_ - i;
        if (pitch < 0 || pitch > 127)
            continue;
        const int y = yForPitch(pitch);
        const int pitchClass = pitch % 12;
        const juce::Rectangle<int> rowRect(keysColumnBounds_.getX(), y, keysColumnBounds_.getWidth(),
                                           (int)kPixelsPerSemitone);
        if (isBlackKeyPitchClass(pitchClass)) {
            g.setColour(blackKey);
            g.fillRect(rowRect);
        }
        g.setColour(sep.withAlpha(0.5f));
        g.drawHorizontalLine(y, (float)rowRect.getX(), (float)rowRect.getRight());

        if (pitchClass == 0) { // C: label the octave, mono font resolved via the LnF (never a raw
                               // family swap — see class comment / CLAUDE.md).
            g.setColour(textCol);
            g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), microSize, juce::Font::plain));
            g.drawText("C" + juce::String(pitch / 12 - 1), rowRect.reduced(3, 0), juce::Justification::centredLeft,
                       false);
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

    // Quantise button: lit (accent) only when there is a grid to quantise to (Snap != Off).
    g.setColour(currentGridBeats() > 0.0 ? accent : textCol.withAlpha(0.4f));
    g.drawText("Q", quantiseButtonBounds_, juce::Justification::centred, false);
    g.setColour(border);
    g.drawRoundedRectangle(quantiseButtonBounds_.toFloat(), 3.0f, 1.0f);
}

void PianoRollComponent::paintMarquee(juce::Graphics& g) {
    if (marqueeRect_.isEmpty())
        return;
    g.setColour(juce::Colours::white.withAlpha(0.12f));
    g.fillRect(marqueeRect_);
    g.setColour(juce::Colours::white.withAlpha(0.6f));
    g.drawRect(marqueeRect_, 1);
}

//==============================================================================
void PianoRollComponent::resized() {
    auto bounds = getLocalBounds();
    auto header = bounds.removeFromTop(kHeaderHeight);
    backButtonBounds_ = header.removeFromLeft(60).reduced(3, 2);
    header.removeFromLeft(4);
    quantiseButtonBounds_ = header.removeFromLeft(20).reduced(2, 2);

    keysColumnBounds_ = bounds.removeFromLeft(kKeysColumnWidth);
    noteGridBounds_ = bounds; // hit-testing/reporting only — see the class comment: note x
                              // positions use viewState_.beatToX(beat) directly, unmodified by
                              // this rect's getX(), so they line up with the playhead.
}

//==============================================================================
// ---- Editing gestures ----

void PianoRollComponent::clampToClipWindow(double& start, double& length) const {
    const auto* clip = doc_ != nullptr ? doc_->getClip(clipId_) : nullptr;
    const double clipLength = clip != nullptr ? clip->lengthBeats : 0.0;
    start = juce::jlimit(0.0, clipLength, start);
    length = juce::jlimit(0.0, clipLength - start, length);
}

void PianoRollComponent::beginDraw(juce::Point<int> pos) {
    const auto* clip = doc_->getClip(clipId_);
    if (clip == nullptr)
        return;

    const double grid = currentGridBeats();
    const double minLen = grid > 0.0 ? grid : kMinNoteLengthBeats;
    const double rawStart = viewState_.xToBeat((double)pos.x) - clip->startBeat;
    double start = snappedBeatAt(rawStart);
    double length = minLen;
    clampToClipWindow(start, length);

    drawStartBeat_ = start;
    drawLength_ = length;
    drawPitch_ = juce::jlimit(0, 127, pitchForY(pos.y));
    dragMode_ = DragMode::Draw;
    mouseDownPos_ = pos;
    selection_.clear(); // drawing a new note replaces whatever was selected (v1 policy)
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

void PianoRollComponent::performQuantise() {
    if (doc_ == nullptr || !clipId_.isValid())
        return;
    const double grid = currentGridBeats();
    if (grid <= 0.0)
        return; // Snap::Off — no grid to quantise to (matches quantiseNotes' own gridBeats<=0 reject)

    const auto selectedIds = selection_.getSelected();
    if (selectedIds.empty()) {
        // Nothing selected: quantise every note in the clip. quantiseNotes has no note-subset
        // overload (TL5-8 design note), so this is the only case it can serve directly. strength
        // 1.0 = hard snap, matching what "Q" implies.
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
    repaint();
}

//==============================================================================
// ---- Mouse ----

void PianoRollComponent::mouseDown(const juce::MouseEvent& e) {
    grabKeyboardFocus();
    dragMode_ = DragMode::None;

    if (doc_ == nullptr || !clipId_.isValid() || doc_->getClip(clipId_) == nullptr)
        return;
    if (!e.mods.isLeftButtonDown())
        return; // no right-click menu in v1

    const auto pos = e.getPosition();
    if (backButtonBounds_.contains(pos)) {
        requestClose();
        return;
    }
    if (quantiseButtonBounds_.contains(pos)) {
        performQuantise();
        return;
    }
    if (pos.y < kHeaderHeight)
        return; // rest of the header strip: inert
    if (pos.x < kKeysColumnWidth)
        return; // keys column: no virtual-keyboard preview in v1

    auto hit = hitTestNote(pos);

    if (hit && e.mods.isCommandDown()) {
        if (!selection_.contains(hit->id))
            selection_.setSelection({hit->id});
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

    // Empty grid. Pencil-by-default (TL5-8 design): a plain drag from empty grid ALWAYS draws a
    // note, so — unlike TimelineClipLaneArea's deferred-empty-click trick, which exists to
    // disambiguate "deselect" from "marquee" — no deferred state is needed here at all: the choice
    // between draw and marquee is fully decided by whether Shift is down at mouseDown.
    if (e.mods.isShiftDown()) {
        beginMarquee(pos, e.mods.isCommandDown() || e.mods.isCtrlDown());
        return;
    }

    beginDraw(pos);
    repaint();
}

void PianoRollComponent::mouseDrag(const juce::MouseEvent& e) {
    if (dragMode_ == DragMode::Marquee) {
        updateMarquee(e.getPosition());
        return;
    }
    if (dragMode_ == DragMode::None || doc_ == nullptr || !clipId_.isValid())
        return;

    const auto* clip = doc_->getClip(clipId_);
    if (clip == nullptr) {
        dragMode_ = DragMode::None;
        return;
    }

    const auto pos = e.getPosition();

    if (dragMode_ == DragMode::Draw) {
        const double grid = currentGridBeats();
        const double minLen = grid > 0.0 ? grid : kMinNoteLengthBeats;
        const double rawEnd = viewState_.xToBeat((double)pos.x) - clip->startBeat;
        const double snappedEnd = snappedBeatAt(rawEnd);
        double length = std::max(snappedEnd - drawStartBeat_, minLen);
        length = std::min(length, std::max(0.0, clip->lengthBeats - drawStartBeat_));
        drawLength_ = length;
    } else if (dragMode_ == DragMode::Move) {
        double anchorOriginalStart = 0.0;
        for (const auto& origin : dragNotes_)
            if (origin.id == activeNote_)
                anchorOriginalStart = origin.startBeat;

        const double deltaBeatsRaw = viewState_.xToBeat((double)pos.x) - viewState_.xToBeat((double)mouseDownPos_.x);
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

        // Pitch delta: one semitone per kPixelsPerSemitone px, clamped so no note in the group
        // leaves [0, 127].
        const int deltaPitchRaw = (int)std::llround(((double)mouseDownPos_.y - (double)pos.y) / kPixelsPerSemitone);
        int minPitch = 127, maxPitch = 0;
        first = true;
        for (const auto& origin : dragNotes_) {
            if (first || origin.pitch < minPitch)
                minPitch = origin.pitch;
            if (first || origin.pitch > maxPitch)
                maxPitch = origin.pitch;
            first = false;
        }
        previewDeltaPitch_ = juce::jlimit(-minPitch, 127 - maxPitch, deltaPitchRaw);
    } else if (dragMode_ == DragMode::Resize) {
        const auto* note = doc_->getNote(activeNote_);
        if (note == nullptr)
            return;
        const double grid = currentGridBeats();
        const double minLen = grid > 0.0 ? grid : kMinNoteLengthBeats;
        const double rawEnd = viewState_.xToBeat((double)pos.x) - clip->startBeat;
        const double snappedEnd = snappedBeatAt(rawEnd);
        double length = std::max(snappedEnd - note->startBeat, minLen);
        length = std::min(length, std::max(0.0, clip->lengthBeats - note->startBeat));
        previewLength_ = length;
    } else if (dragMode_ == DragMode::VelocityScrub) {
        // ~1 per px, up = louder (screen y decreases as pitch/velocity "increases" — matching
        // yForPitch's convention). Clamped per-note (independently) in effectiveGeometryFor.
        previewDeltaVelocity_ = mouseDownPos_.y - pos.y;
    }

    repaint();
}

void PianoRollComponent::mouseUp(const juce::MouseEvent&) {
    if (dragMode_ == DragMode::Marquee) {
        endMarquee();
        dragMode_ = DragMode::None;
        repaint();
        return;
    }

    if (doc_ == nullptr || !clipId_.isValid()) {
        dragMode_ = DragMode::None;
        return;
    }

    if (dragMode_ == DragMode::Draw) {
        const auto pitch = drawPitch_;
        const auto start = drawStartBeat_;
        const auto length = drawLength_;
        synth::NoteId newId;
        auto mutate = [this, pitch, start, length, &newId] {
            synth::MidiNote note;
            note.startBeat = start;
            note.lengthBeats = length;
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
    } else if (dragMode_ == DragMode::Move && (std::abs(previewDeltaBeats_) > 1e-9 || previewDeltaPitch_ != 0)) {
        const auto notes = dragNotes_;
        const double delta = previewDeltaBeats_;
        const int deltaPitch = previewDeltaPitch_;
        auto mutate = [this, notes, delta, deltaPitch] {
            for (const auto& origin : notes)
                doc_->moveNote(origin.id, origin.startBeat + delta, juce::jlimit(0, 127, origin.pitch + deltaPitch));
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
    const auto pos = e.getPosition();
    if (pos.y < kHeaderHeight || pos.x < kKeysColumnWidth)
        return;

    auto hit = hitTestNote(pos);
    if (!hit)
        return;

    const auto id = hit->id;
    auto mutate = [this, id] { doc_->removeNote(id); };
    if (undoManager_)
        undoManager_->recordTimelineChange(*doc_, mutate);
    else
        mutate();
    selection_.remove(id);
    repaint();
}

void PianoRollComponent::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) {
    if (e.mods.isCommandDown()) {
        // Forward verbatim to the panel's existing zoom-around-cursor handling — Component's base
        // implementation walks up to the nearest enabled ancestor, which is exactly what
        // TimelineRulerComponent relies on by simply not overriding this at all. Do not duplicate
        // the zoom math here (TL5-8 design).
        juce::Component::mouseWheelMove(e, wheel);
        return;
    }

    if (std::abs(wheel.deltaX) > std::abs(wheel.deltaY)) {
        const double deltaBeats = -(double)wheel.deltaX * kScrollPixelsPerWheelUnit / viewState_.pixelsPerBeat;
        viewState_.scrollBeats(deltaBeats);
        repaint();
    } else if (wheel.deltaY != 0.0f) {
        const int deltaRows = (int)std::llround(-(double)wheel.deltaY * kPitchScrollSemitonesPerWheelUnit);
        firstVisiblePitch_ = juce::jlimit(0, 127, firstVisiblePitch_ + deltaRows);
        repaint();
    }
}

//==============================================================================
bool PianoRollComponent::keyPressed(const juce::KeyPress& key) {
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

    return false;
}

} // namespace synth::ui
