#include "PianoRollComponent.h"
#include "../AppUndoManager.h"
#include "../Transport/TransportService.h"
#include "Theme/AppLookAndFeel.h"
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

// Below this spacing a level of gridlines is dropped entirely — the same adaptive-density idea the
// panel's own bar/beat grid uses, applied per level (bars, beats, snap division).
constexpr double kMinGridLinePixels = 3.0;

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
}

//==============================================================================
// ---- Entry/exit ----

void PianoRollComponent::openClip(synth::ClipId id) {
    dragMode_ = DragMode::None;
    selection_.clear();
    // The line has to be re-announced against the new framing before it is drawn again.
    hasPlayheadX_ = false;
    // The hovered cut belonged to a note in the OLD clip. (The clipboard deliberately survives —
    // see copySelectedNotes.)
    hasSplitPreview_ = false;
    splitPreviewNote_ = {};

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

    const int visibleRows = std::max(1, (getHeight() - kHeaderHeight) / (int)pixelsPerSemitone_);
    const int median = medianPitchOf(clip->notes);
    firstVisiblePitch_ = juce::jlimit(0, 127, median + visibleRows / 2);

    // Frame the clip: its start at the keys column's right edge, zoomed so the whole clip fits the
    // grid width (subject to the shared zoom clamps).
    const double gridWidth = std::max(1.0, (double)(getWidth() - kKeysColumnWidth));
    const double fitted = clip->lengthBeats > 0.0 ? gridWidth / clip->lengthBeats : rollView_.pixelsPerBeat;
    setHorizontalView(fitted, clip->startBeat);
    repaint();
}

void PianoRollComponent::closeRoll() {
    clipId_ = {};
    selection_.clear();
    dragMode_ = DragMode::None;
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
    repaint();
}

//==============================================================================
// ---- Geometry: the roll's OWN horizontal mapping ----

double PianoRollComponent::beatToX(double absBeat) const noexcept {
    return (double)kKeysColumnWidth + rollView_.beatToX(absBeat);
}

double PianoRollComponent::xToBeat(double x) const noexcept { return rollView_.xToBeat(x - (double)kKeysColumnWidth); }

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

int PianoRollComponent::yForPitch(int pitch) const noexcept {
    return kHeaderHeight + (int)std::llround((double)(firstVisiblePitch_ - pitch) * pixelsPerSemitone_);
}

int PianoRollComponent::pitchForY(int y) const noexcept {
    return firstVisiblePitch_ - (int)std::floor((double)(y - kHeaderHeight) / pixelsPerSemitone_);
}

juce::Rectangle<int> PianoRollComponent::gridRegion() const noexcept {
    return {kKeysColumnWidth, kHeaderHeight, std::max(0, getWidth() - kKeysColumnWidth),
            std::max(0, getHeight() - kHeaderHeight)};
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
    if (!std::isfinite(spacingBeats) || spacingBeats <= 0.0)
        return range;
    if (spacingBeats * rollView_.pixelsPerBeat < kMinGridLinePixels)
        return range; // too dense to read — this level is dropped entirely

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

void PianoRollComponent::paintGridLines(juce::Graphics& g, juce::Colour lineColour) {
    const auto grid = gridRegion();
    const float top = (float)grid.getY();
    const float bottom = (float)grid.getBottom();

    // Faintest level first so a bar line always wins a pixel it shares with a beat or sub-beat line.
    const auto drawLevel = [&](double spacingBeats, float alpha) {
        const auto range = visibleLineRange(spacingBeats);
        g.setColour(lineColour.withAlpha(alpha));
        for (long long i = range.first; i <= range.last; ++i) {
            const int x = (int)std::llround(beatToX((double)i * spacingBeats));
            if (x < grid.getX() || x > grid.getRight())
                continue;
            g.drawVerticalLine(x, top, bottom);
        }
    };

    const double division = currentGridBeats(); // 0.0 == Snap::Off: no sub-beat level to draw
    if (division > 0.0 && division < 1.0)
        drawLevel(division, 0.14f);
    drawLevel(1.0, 0.30f);
    drawLevel(currentBeatsPerBar(), 0.75f);
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
    for (int i = -1; i <= visibleRows; ++i) {
        const int pitch = firstVisiblePitch_ - i;
        if (pitch < 0 || pitch > 127)
            continue;
        const int y = yForPitch(pitch);
        g.setColour(isBlackKeyPitchClass(pitch % 12) ? blackRow : whiteRow);
        g.fillRect(0, y, width, rowHeight);
        g.setColour(rowSep.withAlpha(0.35f));
        g.drawHorizontalLine(y, 0.0f, (float)width);
    }

    // Vertical lines at the CURRENT snap division (faint), beats (medium) and bars (strong) — pure
    // state, redrawn whenever the snap selector changes because the panel repaints us then.
    paintGridLines(g, rowSep);

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

    // A MUTED note reads as an empty outline: it is still there, still selectable, still exactly
    // where it was — it just makes no sound, so it must not carry the same visual weight as a note
    // that does. The flag is read from the DOC, never from the snapshot, because a muted note is
    // excluded at flatten time and so is simply absent downstream (see MidiNote::muted). The
    // selection halo survives the dimming: muting a selected note must not look like deselecting it.
    if (note.muted) {
        g.setColour(fill.withMultipliedAlpha(0.12f));
        g.fillRoundedRectangle(bodyBounds, 2.0f);
        g.setColour(selected ? accent : noteColour.withAlpha(0.55f));
        g.drawRoundedRectangle(bodyBounds, 2.0f, selected ? 2.0f : 1.0f);
        return;
    }

    g.setColour(fill);
    g.fillRoundedRectangle(bodyBounds, 2.0f);
    g.setColour(selected ? accent : noteColour.darker(0.5f));
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

    const int rowHeight = std::max(1, (int)pixelsPerSemitone_);
    const int visibleRows = std::max(0, keysColumnBounds_.getHeight() / rowHeight) + 2;
    for (int i = -1; i <= visibleRows; ++i) {
        const int pitch = firstVisiblePitch_ - i;
        if (pitch < 0 || pitch > 127)
            continue;
        const int y = yForPitch(pitch);
        const int pitchClass = pitch % 12;
        const juce::Rectangle<int> rowRect(keysColumnBounds_.getX(), y, keysColumnBounds_.getWidth(), rowHeight);
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
    dragNotes_.clear();
    previewDeltaBeats_ = 0.0;
    previewDeltaPitch_ = 0;
    previewDeltaVelocity_ = 0;
    drawLengthBeats_ = 0.0;
    clearSplitPreview();

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
        pos.x >= kKeysColumnWidth) {
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
    if (doc_ != nullptr && clipId_.isValid() && pos.y >= kHeaderHeight && pos.x >= kKeysColumnWidth) {
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
    return quantiseButtonBounds_.contains(pos) ? juce::String(kQuantiseTooltip) : juce::String();
}

juce::String PianoRollComponent::getTooltip() { return getTooltipFor(getMouseXYRelative()); }

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
        flashQuantiseButton(); // feedback even when the click is a no-op
        if (e.mods.isShiftDown())
            performQuantise(); // one-shot: snap the existing notes to the grid
        else
            toggleSnap(); // plain click: the grid-magnetism switch
        return;
    }
    if (pos.y < kHeaderHeight)
        return; // rest of the header strip: inert
    if (pos.x < kKeysColumnWidth)
        return; // keys column: no virtual-keyboard preview in v1

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

    // Empty grid. Shift arms the marquee; anything else is a plain click-through that DESELECTS.
    // Creating a note is the double-click (mouseDoubleClick) — a single click never draws.
    if (e.mods.isShiftDown()) {
        beginMarquee(pos, e.mods.isCommandDown() || e.mods.isCtrlDown());
        return;
    }

    if (!selection_.isEmpty()) {
        selection_.clear();
        repaint();
    }
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

        const double deltaBeatsRaw = xToBeat((double)pos.x) - xToBeat((double)mouseDownPos_.x);
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

        // Pitch delta: one semitone per row of the CURRENT vertical zoom, clamped so no note in the
        // group leaves [0, 127].
        const int deltaPitchRaw = (int)std::llround(((double)mouseDownPos_.y - (double)pos.y) / pixelsPerSemitone_);
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
        const double rawEnd = xToBeat((double)pos.x) - clip->startBeat;
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
    // Create-and-delete-by-double-click belongs to the SELECT tool. Under any other tool both
    // clicks have already been handled as that tool's own single-click action, and adding a note
    // on top of (say) a second Erase click would be a gesture nobody asked for.
    if (activeTool_ != EditTool::Select)
        return;
    const auto pos = e.getPosition();
    if (pos.y < kHeaderHeight || pos.x < kKeysColumnWidth)
        return;

    // JUCE dispatches this AFTER the second mouseDown/mouseUp pair, so whatever those did (select a
    // note, deselect on empty grid) has already happened — this is the last word either way.
    dragMode_ = DragMode::None;

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
        // Vertical zoom, keeping the pitch under the cursor put. firstVisiblePitch_ is an int, so
        // the anchor holds to within one row — enough for a wheel gesture.
        const double rowsAbove = ((double)pos.y - (double)kHeaderHeight) / pixelsPerSemitone_;
        const double anchorPitch = (double)firstVisiblePitch_ - rowsAbove;
        setPixelsPerSemitone(pixelsPerSemitone_ * std::exp((double)wheel.deltaY * kZoomWheelSensitivity));
        const double newRowsAbove = ((double)pos.y - (double)kHeaderHeight) / pixelsPerSemitone_;
        firstVisiblePitch_ = juce::jlimit(0, 127, (int)std::llround(anchorPitch + newRowsAbove));
        repaint();
        return;
    }

    if (command) {
        // Horizontal zoom around the beat under the cursor, through the roll's OWN mapping — the
        // shared TimelineViewState must not move (the lanes behind us keep their own zoom). Same
        // exponential factor the panel's ruler zoom uses, so the two feel identical.
        const double anchorGridX = std::max(0.0, (double)pos.x - (double)kKeysColumnWidth);
        rollView_.zoomAroundX(std::exp((double)wheel.deltaY * kZoomWheelSensitivity), anchorGridX);
        repaint();
        if (onHorizontalViewChanged)
            onHorizontalViewChanged();
        return;
    }

    // Shift+wheel is horizontal scroll; so is a trackpad's own horizontal delta. Some platforms
    // already swap the axes under Shift, so take whichever delta actually carries the gesture.
    const bool horizontal = shift || std::abs(wheel.deltaX) > std::abs(wheel.deltaY);
    if (horizontal) {
        const double delta =
            std::abs(wheel.deltaX) > std::abs(wheel.deltaY) ? (double)wheel.deltaX : (double)wheel.deltaY;
        if (delta != 0.0) {
            rollView_.scrollBeats(-delta * kScrollPixelsPerWheelUnit / rollView_.pixelsPerBeat);
            repaint();
            if (onHorizontalViewChanged)
                onHorizontalViewChanged();
        }
        return;
    }

    if (wheel.deltaY != 0.0f) {
        const int deltaRows = (int)std::llround(-(double)wheel.deltaY * kPitchScrollSemitonesPerWheelUnit);
        firstVisiblePitch_ = juce::jlimit(0, 127, firstVisiblePitch_ + deltaRows);
        repaint();
    }
}

void PianoRollComponent::mouseMagnify(const juce::MouseEvent& e, float scaleFactor) {
    // Trackpad pinch, same pair as the panel's: plain = horizontal zoom around the pinch point
    // (through the roll's OWN mapping), Shift = vertical (pixels-per-semitone) zoom.
    if (!std::isfinite(scaleFactor) || scaleFactor <= 0.0f)
        return;
    const auto pos = e.getPosition();
    if (e.mods.isShiftDown()) {
        const double rowsAbove = ((double)pos.y - (double)kHeaderHeight) / pixelsPerSemitone_;
        const double anchorPitch = (double)firstVisiblePitch_ - rowsAbove;
        setPixelsPerSemitone(pixelsPerSemitone_ * (double)scaleFactor);
        const double newRowsAbove = ((double)pos.y - (double)kHeaderHeight) / pixelsPerSemitone_;
        firstVisiblePitch_ = juce::jlimit(0, 127, (int)std::llround(anchorPitch + newRowsAbove));
        repaint();
        return;
    }
    rollView_.zoomAroundX((double)scaleFactor, std::max(0.0, (double)pos.x - (double)kKeysColumnWidth));
    repaint();
    if (onHorizontalViewChanged)
        onHorizontalViewChanged();
}

//==============================================================================
bool PianoRollComponent::keyPressed(const juce::KeyPress& key) {
    // Q = toggle grid magnetism; Shift+Q = one-shot quantise (same pair as the header button's
    // click / Shift+click). Handled here so it works while the roll has focus; the panel handles
    // the same key for every other focus target inside the timeline. Matched on the key CODE
    // (JUCE letter key codes are the uppercase character), so the Shift variant still matches.
    if (key.getKeyCode() == 'Q' || key.getKeyCode() == 'q') {
        flashQuantiseButton();
        if (key.getModifiers().isShiftDown())
            performQuantise();
        else
            toggleSnap();
        return true;
    }

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

    // Arrow keys edit the SELECTION and nothing else: with nothing selected they fall through, so
    // the panel (and the graph behind it) keep whatever those keys mean there. Matched on the key
    // CODE because juce::KeyPress::operator==(int) also requires no modifiers, which would miss
    // Shift+Up. Digit keys are deliberately NOT handled here at all — tool switching belongs to
    // the panel (see setActiveTool).
    const int code = key.getKeyCode();
    if (code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey)
        return nudgeSelectedNotes(code == juce::KeyPress::rightKey ? 1 : -1);
    if (code == juce::KeyPress::upKey || code == juce::KeyPress::downKey) {
        // Shift is the octave jump, the same 12-semitone convention every DAW uses.
        const int step = key.getModifiers().isShiftDown() ? 12 : 1;
        return transposeSelectedNotes(code == juce::KeyPress::upKey ? step : -step);
    }

    return false;
}

} // namespace synth::ui
