#include "TimelineClipLaneArea.h"
#include "../AppUndoManager.h"
#include "../Modules/RecordTapModule.h"
#include "../ShortcutManager.h"
#include "../Transport/TransportService.h"
#include "Theme/AppLookAndFeel.h"
#include "TimelineTrackHeaderComponent.h"
#include "ToolCursors.h"
#include "TrackColour.h"
#include <algorithm>
#include <cmath>
#include <set>

namespace synth::ui {

namespace {
// Edge-drag zones (right = resize length, left = move+resize keeping the end fixed).
constexpr int kEdgeZonePx = 6;

// TimelineDoc has no explicit minimum clip length; this mirrors TimelineViewState::Snap's own
// finest grid (Sixteenth = 1/16 beat, the same unit TransportService's kMinLoopLengthBeats uses)
// so a right-edge trim can never collapse a clip past what the grid itself can represent.
constexpr double kMinClipLengthBeats = 0.0625;

constexpr int kMinWidthForName = 40;
constexpr int kMinWidthForNotePreview = 24;

// Same numeric threshold as kMinWidthForNotePreview (a named twin rather than a shared
// constant — a note preview and a waveform are unrelated concepts that happen to agree today).
constexpr int kMinWidthForWaveform = 24;

// bpm/sampleRate fallbacks when there is no live transport (a headless test, or a
// TimelineClipLaneArea built with setTransport() never called) — the same "no transport, assume
// 120 bpm" convention currentBeatsPerBar() uses for beatsPerBar, and the same 44.1 kHz fallback
// MainComponent's own audio-take code uses when a snapshot's sampleRate is not yet known.
constexpr double kFallbackBpm = 120.0;
constexpr double kFallbackSampleRate = 44100.0;

// The empty-row hint (see TimelineClipLaneArea::emptyRowHintFor). Escaped UTF-8 for the em dash so
// the source file stays plain ASCII, the same way the theme/label strings elsewhere spell one.
constexpr const char* kMidiEmptyRowHint = "Double-click to add a clip \xE2\x80\x94 or arm (R) and record";
constexpr const char* kAudioEmptyRowHint = "Drop an audio file \xE2\x80\x94 or arm (R) and record";

// The hint is dropped rather than clipped or shrunk below these: a row shorter than this has no
// room for an 11 px line, and one narrower than the text plus its padding would truncate mid-word.
constexpr int kMinRowHeightForHint = 24;
constexpr int kHintPaddingPx = 8;
constexpr float kHintFontHeight = 11.0f;

// Half-width of the Split tool's preview line's repaint region. The stroke itself is 1 px; the
// margin is what keeps an antialiased line from leaving a fringe outside the repainted column.
constexpr int kSplitPreviewMarginPx = 2;

// A copy-drag ghost: a translucent wash of the SOURCE track's colour, well under the 0.65/0.85 a
// real clip body paints at, plus a soft one-pixel outline. Translucency alone carries "not real
// yet" on purpose — a true blur would mean rendering the region to an image and filtering it once
// per drag frame, which is exactly the kind of unbounded per-frame paint work docs/layout.md
// §10-11 rules out.
constexpr float kDragGhostFillAlpha = 0.4f;
constexpr float kDragGhostOutlineAlpha = 0.7f;

// A muted clip keeps its shape, its selection border and its waveform/notes, and loses only
// brightness — mute is reversible, so it must not look like damage. The name label dims further
// than the body (it is the one part a glance reads as "this clip is fine").
constexpr float kMutedClipLabelAlpha = 0.35f;

// The icon each tool's cursor is rendered from — the SAME already-tinted Drawable the tool
// strip's button paints, which is what keeps cursor and button in sync across themes (see
// synth::ui::makeToolCursor).
synth::theme::Icon iconForTool(synth::ui::EditTool tool) noexcept {
    using synth::theme::Icon;
    switch (tool) {
    case synth::ui::EditTool::Select:
        return Icon::ToolSelect;
    case synth::ui::EditTool::Split:
        return Icon::ToolSplit;
    case synth::ui::EditTool::Glue:
        return Icon::ToolGlue;
    case synth::ui::EditTool::Erase:
        return Icon::ToolErase;
    case synth::ui::EditTool::Mute:
        return Icon::ToolMute;
    case synth::ui::EditTool::Draw:
        return Icon::ToolDraw;
    }
    return Icon::ToolSelect;
}
} // namespace

//==============================================================================
TimelineClipLaneArea::TimelineClipLaneArea(TimelineViewState& viewState, ClipSelectionModel& selection)
    : viewState_(viewState)
    , selection_(selection) {
    setComponentID("timelineClipLaneArea");
    setInterceptsMouseClicks(true, false);
    // Load-bearing, and invisible to every headless test: juce::grabKeyboardFocus() is a NO-OP on
    // a component that does not want focus, so without this the mouseDown() call below never moves
    // focus here — MainComponent::resolveEditSurface() then finds whatever had focus before,
    // falls through to EditSurface::Graph, and every per-surface verb (Cmd+X/C/V/D) plus this
    // class's own Delete/Escape/P go to the patch canvas instead of the clips the user just
    // clicked. GraphEditor and PianoRollComponent set the same flag for the same reason. The
    // focus-override tests (MainComponent::editSurfaceOverrideForTest_) bypass real focus
    // entirely, which is exactly why this hole survived until a user hit it — see
    // ClipLaneAcceptsKeyboardFocusSoSurfaceVerbsCanRoute for the guard that now pins it.
    setWantsKeyboardFocus(true);
    audioFormats_.registerBasicFormats();
    // The production chooser, installed as the DEFAULT rather than called directly, so a test can
    // replace it wholesale (see setAudioFileChooser).
    audioFileChooser_ = [this](std::function<void(const juce::File&)> onChosen) {
        launchAudioFileChooser(std::move(onChosen));
    };
}

//==============================================================================
void TimelineClipLaneArea::setTimelineDoc(synth::TimelineDoc* doc) {
    doc_ = doc;
    refreshFromDoc();
}

void TimelineClipLaneArea::refreshFromDoc() {
    std::vector<synth::ClipId> alive;
    if (doc_ != nullptr) {
        for (const auto& track : doc_->getTracks())
            for (const auto& clip : track.clips)
                alive.push_back(clip.id);
    }
    selection_.retainOnly(alive);
    // A rename in flight over a clip this mutation removed has nothing left to commit to — drop
    // the editor rather than let a later Return call setClipName on a dead id.
    if (renameEditor_ != nullptr && (doc_ == nullptr || doc_->getClip(renamingClip_) == nullptr))
        finishRename(false);
    // The Split tool's hover line is doc geometry too: forget it rather than draw it against a
    // clip that may have moved or gone.
    clearToolPreviews();
    // Simplest-correct cache policy (see the class comment) — ANY doc change clears every
    // cached synth::PeaksFile::Data rather than diffing which assetRefs actually moved. Peaks
    // files are small, so the next paint's re-resolve+re-read is cheap; the alternative (per-ref
    // dirty tracking against a mutation we don't otherwise inspect) is not worth building yet.
    peaksCache_.clear();
    // Same policy, same reasoning, for the asset-existence cache.
    assetExistsCache_.clear();
    repaint();
}

void TimelineClipLaneArea::setPeaksResolver(std::function<juce::File(const juce::String&)> resolver) {
    peaksResolver_ = std::move(resolver);
    invalidatePeaksCache(); // a different resolver may resolve an already-cached ref differently
}

void TimelineClipLaneArea::invalidatePeaksCache() {
    peaksCache_.clear();
    // Cleared alongside — see setAssetExistsResolver's comment for why the two caches share
    // every clear point.
    assetExistsCache_.clear();
    repaint();
}

void TimelineClipLaneArea::setAssetExistsResolver(std::function<bool(const juce::String&)> resolver) {
    assetExistsResolver_ = std::move(resolver);
    invalidatePeaksCache(); // a different resolver may answer an already-cached ref differently
}

int TimelineClipLaneArea::getRowHeight() const {
    int base = TimelineTrackHeaderComponent::kRowHeight;
    if (auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel()))
        base = lf->getTheme().metrics.timelineTrackRowHeight;
    // Vertical zoom: the shared scale keeps this in lock-step with the header column's rows
    // (TimelinePanelComponent::layoutTrackHeaders applies the same factor).
    return std::max(8, (int)std::llround((double)base * viewState_.rowHeightScale));
}

double TimelineClipLaneArea::currentBeatsPerBar() const {
    double beatsPerBar = 4.0;
    if (transport_ != nullptr) {
        const auto snap = transport_->getPositionSnapshot();
        const double tsBeatsPerBar = (double)snap.timeSigNumerator * 4.0 / (double)std::max(1, snap.timeSigDenominator);
        if (tsBeatsPerBar > 0.0)
            beatsPerBar = tsBeatsPerBar;
    }
    return beatsPerBar;
}

double TimelineClipLaneArea::snappedBeatAt(double rawBeat) const {
    return viewState_.snapBeat(rawBeat, currentBeatsPerBar());
}

double TimelineClipLaneArea::floorSnappedBeatAt(double rawBeat) const {
    const double division = viewState_.divisionBeats(currentBeatsPerBar());
    if (division <= 0.0)
        return std::max(0.0, rawBeat); // Snap::Off — no grid to floor onto
    return std::max(0.0, std::floor(rawBeat / division) * division);
}

double TimelineClipLaneArea::ceilSnappedBeatAt(double rawBeat) const {
    const double division = viewState_.divisionBeats(currentBeatsPerBar());
    if (division <= 0.0)
        return std::max(0.0, rawBeat); // Snap::Off — no grid to reach up to
    return std::max(0.0, std::ceil(rawBeat / division) * division);
}

double TimelineClipLaneArea::minDrawLengthBeats() const {
    const double division = viewState_.divisionBeats(currentBeatsPerBar());
    return division > 0.0 ? division : kMinClipLengthBeats;
}

std::optional<int> TimelineClipLaneArea::trackIndexAt(juce::Point<int> pos) const {
    if (doc_ == nullptr || pos.y < 0)
        return std::nullopt;
    const int rowHeight = getRowHeight();
    if (rowHeight <= 0)
        return std::nullopt;
    // Same vertical-scroll offset rowBounds() subtracts — a hit test and a painted row must never
    // disagree about which track a y lands in.
    const int contentY = pos.y + (int)std::llround(viewState_.trackScrollY);
    if (contentY < 0)
        return std::nullopt;
    const int index = contentY / rowHeight;
    if (index >= (int)doc_->getTracks().size())
        return std::nullopt; // below the last row: empty panel space, not a row
    return index;
}

juce::Rectangle<int> TimelineClipLaneArea::rowBounds(int trackIndex, int rowHeight) const {
    return {0, trackIndex * rowHeight - (int)std::llround(viewState_.trackScrollY), getWidth(), rowHeight};
}

//==============================================================================
juce::Rectangle<int> TimelineClipLaneArea::computeClipRect(const TimelineViewState& viewState, int trackIndex,
                                                           double startBeat, double lengthBeats, int rowHeight) {
    const double x0 = viewState.beatToX(startBeat);
    const double x1 = viewState.beatToX(startBeat + lengthBeats);
    const int left = (int)std::llround(x0);
    const int right = (int)std::llround(x1);
    return {left, trackIndex * rowHeight - (int)std::llround(viewState.trackScrollY), std::max(right - left, 1),
            rowHeight};
}

TimelineClipLaneArea::BucketRange TimelineClipLaneArea::bucketRangeForClip(const synth::PeaksFile::Data& peaks,
                                                                           double lengthBeats,
                                                                           double sourceStartSeconds, double bpm,
                                                                           double sampleRate) {
    BucketRange range;
    if (peaks.numChannels <= 0 || peaks.bucketSize <= 0 || sampleRate <= 0.0)
        return range;

    const int totalBuckets = (int)(peaks.buckets.size() / (std::size_t)peaks.numChannels);
    if (totalBuckets <= 0)
        return range;

    const double secondsPerBeat = bpm > 0.0 ? 60.0 / bpm : 60.0 / kFallbackBpm;
    const double startSeconds = std::max(0.0, sourceStartSeconds);
    const double endSeconds = startSeconds + std::max(0.0, lengthBeats) * secondsPerBeat;

    const double startSample = startSeconds * sampleRate;
    const double endSample = endSeconds * sampleRate;

    int firstBucket = (int)std::floor(startSample / (double)peaks.bucketSize);
    int lastBucketExclusive = (int)std::ceil(endSample / (double)peaks.bucketSize);

    firstBucket = juce::jlimit(0, totalBuckets, firstBucket);
    lastBucketExclusive = juce::jlimit(firstBucket, totalBuckets, lastBucketExclusive);

    range.firstBucket = firstBucket;
    range.bucketCount = lastBucketExclusive - firstBucket;
    return range;
}

juce::Rectangle<int> TimelineClipLaneArea::getClipRect(synth::ClipId id) const {
    if (doc_ == nullptr)
        return {};
    const int rowHeight = getRowHeight();
    const auto& tracks = doc_->getTracks();
    for (int trackIndex = 0; trackIndex < (int)tracks.size(); ++trackIndex)
        for (const auto& clip : tracks[(size_t)trackIndex].clips)
            if (clip.id == id)
                return computeClipRect(viewState_, trackIndex, clip.startBeat, clip.lengthBeats, rowHeight);
    return {};
}

std::vector<std::pair<synth::ClipId, juce::Rectangle<int>>> TimelineClipLaneArea::collectClipRects() const {
    std::vector<std::pair<synth::ClipId, juce::Rectangle<int>>> rects;
    if (doc_ == nullptr)
        return rects;
    const int rowHeight = getRowHeight();
    const auto& tracks = doc_->getTracks();
    for (int trackIndex = 0; trackIndex < (int)tracks.size(); ++trackIndex)
        for (const auto& clip : tracks[(size_t)trackIndex].clips)
            rects.emplace_back(clip.id,
                               computeClipRect(viewState_, trackIndex, clip.startBeat, clip.lengthBeats, rowHeight));
    return rects;
}

std::optional<TimelineClipLaneArea::ClipHit> TimelineClipLaneArea::hitTestClip(juce::Point<int> pos) const {
    if (doc_ == nullptr)
        return std::nullopt;

    const int rowHeight = getRowHeight();
    const auto& tracks = doc_->getTracks();
    for (int trackIndex = 0; trackIndex < (int)tracks.size(); ++trackIndex) {
        for (const auto& clip : tracks[(size_t)trackIndex].clips) {
            const auto rect = computeClipRect(viewState_, trackIndex, clip.startBeat, clip.lengthBeats, rowHeight);
            if (!rect.contains(pos))
                continue;

            ClipHit hit{clip.id, rect, ClipHit::Zone::Body};
            if (pos.x <= rect.getX() + kEdgeZonePx)
                hit.zone = ClipHit::Zone::LeftEdge;
            else if (pos.x >= rect.getRight() - kEdgeZonePx)
                hit.zone = ClipHit::Zone::RightEdge;
            return hit;
        }
    }
    return std::nullopt;
}

TimelineClipLaneArea::Geometry TimelineClipLaneArea::effectiveGeometryFor(const synth::Clip& clip) const {
    // The copy-drag guard has to match effectiveRowFor's EXACTLY: the two together decide where a
    // clip paints, and a copy-drag's promise is that the original does not move on ANY axis while
    // the ghosts do. Guarding only the row (as this did before) slid every original sideways under
    // the pointer, which reads as a move that also happens to be drawing outlines — the opposite
    // of what Alt means.
    if (dragMode_ == DragMode::Move && !copyDrag_) {
        for (const auto& origin : dragClips_)
            if (origin.id == clip.id)
                return {origin.originalStart + previewDeltaBeats_, origin.lengthBeats};
    } else if (dragMode_ == DragMode::ResizeRight && activeClip_ == clip.id) {
        return {resizeOriginalStart_, previewLength_};
    } else if (dragMode_ == DragMode::ResizeLeft && activeClip_ == clip.id) {
        return {previewStart_, previewLength_};
    }
    return {clip.startBeat, clip.lengthBeats};
}

int TimelineClipLaneArea::effectiveRowFor(synth::ClipId id, int trackIndex) const {
    // Same copyDrag_ guard as effectiveGeometryFor above, and it must stay the same — see there.
    if (dragMode_ != DragMode::Move || copyDrag_ || previewRowDelta_ == 0)
        return trackIndex;
    for (const auto& origin : dragClips_)
        if (origin.id == id)
            return trackIndex + previewRowDelta_;
    return trackIndex;
}

//==============================================================================
void TimelineClipLaneArea::paint(juce::Graphics& g) {
    if (doc_ == nullptr)
        return;

    const int rowHeight = getRowHeight();
    const auto& tracks = doc_->getTracks();
    for (int trackIndex = 0; trackIndex < (int)tracks.size(); ++trackIndex) {
        const auto& track = tracks[(size_t)trackIndex];
        const auto bounds = rowBounds(trackIndex, rowHeight);
        // Both of these paint UNDER the row's clips (a drop highlight is a backdrop, and a hint
        // only ever shows on a row that has none).
        if (trackIndex == fileDropRow_)
            paintFileDropHighlight(g, bounds);
        if (track.clips.empty())
            paintEmptyRowHint(g, track, bounds);
        for (const auto& clip : track.clips)
            paintClip(g, clip, track, trackIndex, rowHeight);
    }

    if (liveRecording_.active)
        paintLiveRecordingStrip(g);

    // Tool affordances paint LAST, over the clips they describe.
    paintDragGhosts(g);
    paintDrawGhost(g);
    paintSplitPreview(g);

    if (dragMode_ == DragMode::Marquee)
        paintMarquee(g);
}

void TimelineClipLaneArea::paintClip(juce::Graphics& g, const synth::Clip& clip, const synth::Track& track,
                                     int trackIndex, int rowHeight) {
    const auto geometry = effectiveGeometryFor(clip);
    const auto rect =
        computeClipRect(viewState_, effectiveRowFor(clip.id, trackIndex), geometry.start, geometry.length, rowHeight);
    if (rect.getRight() < 0 || rect.getX() > getWidth())
        return; // cheap offscreen cull — same reasoning as the panel's own bar-line loop

    const bool selected = selection_.contains(clip.id);
    // A clip is dimmed when EITHER its track or the clip itself is muted — the two flags are
    // independent in the model (see synth::Clip::muted) and OR together here for the same reason
    // they OR by omission at flatten time: both mean "you will not hear this". Read from the DOC,
    // never from a TimelineSnapshot, which no longer contains a muted clip at all.
    const juce::Colour base = synth::ui::resolveTrackColour(track.colourArgb, trackIndex, track.muted || clip.muted);
    const juce::Colour fill = selected ? base.brighter(0.15f) : base; // "slight fill lift" when selected

    const auto bodyBounds = rect.toFloat().reduced(1.0f);
    g.setColour(fill.withAlpha(selected ? 0.85f : 0.65f));
    g.fillRoundedRectangle(bodyBounds, 3.0f);

    g.setColour(selected ? base.brighter(0.6f) : base.darker(0.3f));
    g.drawRoundedRectangle(bodyBounds, 3.0f, selected ? 2.0f : 1.0f);

    // assetRef is the MIDI-vs-audio discriminator (see synth::Clip's own comment) — an
    // audio clip gets a waveform instead of the note preview below (its notes vector is empty in
    // every case this build produces, but the branch is on assetRef, not on emptiness, so intent
    // stays explicit even if that ever changes). An audio clip whose asset does not
    // currently resolve gets the missing-asset placeholder instead of an (impossible) waveform.
    if (!clip.assetRef.isEmpty()) {
        if (assetExists(clip.assetRef))
            paintWaveform(g, clip, rect);
        else
            paintMissingAssetPlaceholder(g, clip, rect);
    } else if (rect.getWidth() > kMinWidthForNotePreview) {
        g.setColour(juce::Colours::white.withAlpha(0.55f));
        for (const auto& note : clip.notes) {
            const double noteStartBeat = geometry.start + note.startBeat; // notes are clip-relative
            const double x0 = viewState_.beatToX(noteStartBeat);
            const double x1 = viewState_.beatToX(noteStartBeat + note.lengthBeats);
            const auto nx0 = (float)juce::jlimit((double)rect.getX(), (double)rect.getRight(), x0);
            const auto nx1 = (float)juce::jlimit((double)rect.getX(), (double)rect.getRight(), x1);
            if (nx1 <= nx0)
                continue;

            const float pitchFrac = 1.0f - (float)juce::jlimit(0, 127, note.pitch) / 127.0f;
            const float y = (float)rect.getY() + 2.0f + pitchFrac * (float)(rowHeight - 4);
            g.drawLine(nx0, y, nx1, y, 1.2f);
        }
    }

    if (rect.getWidth() > kMinWidthForName && clip.name.isNotEmpty()) {
        g.setColour(juce::Colours::black.withAlpha(clip.muted ? kMutedClipLabelAlpha : 0.8f));
        g.setFont(juce::Font(11.0f));
        g.drawText(clip.name, rect.reduced(4, 2), juce::Justification::topLeft, true);
    }
}

juce::String TimelineClipLaneArea::emptyRowHintFor(synth::TrackKind kind) {
    switch (kind) {
    case synth::TrackKind::Midi:
        return juce::String::fromUTF8(kMidiEmptyRowHint);
    case synth::TrackKind::Audio:
        return juce::String::fromUTF8(kAudioEmptyRowHint);
    case synth::TrackKind::Automation:
        break; // nothing to author on an automation row — its points come from a lane editor
    }
    return {};
}

juce::String TimelineClipLaneArea::getEmptyRowHintForTest(int trackIndex) const {
    if (doc_ == nullptr || !juce::isPositiveAndBelow(trackIndex, (int)doc_->getTracks().size()))
        return {};
    const auto& track = doc_->getTracks()[(std::size_t)trackIndex];
    return track.clips.empty() ? emptyRowHintFor(track.kind) : juce::String();
}

void TimelineClipLaneArea::paintEmptyRowHint(juce::Graphics& g, const synth::Track& track,
                                             juce::Rectangle<int> bounds) {
    const auto text = emptyRowHintFor(track.kind);
    if (text.isEmpty() || bounds.getHeight() < kMinRowHeightForHint)
        return;

    const juce::Font font{juce::FontOptions(kHintFontHeight)};
    if ((float)bounds.getWidth() < juce::GlyphArrangement::getStringWidth(font, text) + 2.0f * (float)kHintPaddingPx)
        return; // too narrow to read — drop the line rather than truncate it

    // Theme token via the same dynamic_cast<AppLookAndFeel*> pattern getRowHeight() uses, with
    // Theme::Colors::textMuted's own default when headless.
    juce::Colour colour(0xff8A93A0);
    if (auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel()))
        colour = lf->getTheme().colors.textMuted;

    g.setColour(colour.withAlpha(0.75f));
    g.setFont(font);
    g.drawText(text, bounds.reduced(kHintPaddingPx, 0), juce::Justification::centred, false);
}

void TimelineClipLaneArea::paintFileDropHighlight(juce::Graphics& g, juce::Rectangle<int> bounds) {
    juce::Colour accent(0xff00D1FF);
    if (auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel()))
        accent = lf->getTheme().colors.accent;

    g.setColour(accent.withAlpha(0.15f));
    g.fillRect(bounds);
    g.setColour(accent.withAlpha(0.7f));
    g.drawRect(bounds, 2);
}

void TimelineClipLaneArea::paintMarquee(juce::Graphics& g) {
    if (marqueeRect_.isEmpty())
        return;
    g.setColour(juce::Colours::white.withAlpha(0.12f));
    g.fillRect(marqueeRect_);
    g.setColour(juce::Colours::white.withAlpha(0.6f));
    g.drawRect(marqueeRect_, 1);
}

//==============================================================================
// Tool affordances: the copy-drag ghosts, the Draw ghost and the Split preview line. All three
// describe a result that does not exist yet, so all three are drawn well under a real clip's own
// alpha — the Draw ghost and the split line as bare outlines (they have no source to borrow a
// colour from), the copy-drag ghosts as a translucent wash of the source track's colour so the
// user can see WHICH clip each one came from mid-drag.
//==============================================================================

juce::Rectangle<int> TimelineClipLaneArea::dragGhostRectFor(const DragOrigin& origin, int rowHeight) const {
    return computeClipRect(viewState_, origin.trackIndex + previewRowDelta_, origin.originalStart + previewDeltaBeats_,
                           origin.lengthBeats, rowHeight);
}

std::vector<juce::Rectangle<int>> TimelineClipLaneArea::getDragGhostRectsForTest() const {
    std::vector<juce::Rectangle<int>> rects;
    if (dragMode_ != DragMode::Move || !copyDrag_)
        return rects;
    const int rowHeight = getRowHeight();
    rects.reserve(dragClips_.size());
    for (const auto& origin : dragClips_)
        rects.push_back(dragGhostRectFor(origin, rowHeight));
    return rects;
}

std::optional<std::pair<double, double>> TimelineClipLaneArea::getEffectiveGeometryForTest(synth::ClipId id) const {
    if (doc_ == nullptr)
        return std::nullopt;
    const auto* clip = doc_->getClip(id);
    if (clip == nullptr)
        return std::nullopt;
    const auto geometry = effectiveGeometryFor(*clip);
    return std::make_pair(geometry.start, geometry.length);
}

void TimelineClipLaneArea::paintDragGhosts(juce::Graphics& g) {
    if (dragMode_ != DragMode::Move || !copyDrag_ || doc_ == nullptr)
        return;

    const int rowHeight = getRowHeight();
    const auto& tracks = doc_->getTracks();
    for (const auto& origin : dragClips_) {
        const auto rect = dragGhostRectFor(origin, rowHeight);
        if (rect.getRight() < 0 || rect.getX() > getWidth())
            continue; // same offscreen cull paintClip() uses

        // The SOURCE track's colour, not the destination row's: a ghost that changed hue as the
        // pointer crossed rows would read as the clip having already landed there. Muted state
        // comes along for the same reason it does in paintClip() — a copy of a muted clip is
        // still muted when it lands.
        juce::Colour base = juce::Colours::white;
        if (juce::isPositiveAndBelow(origin.trackIndex, (int)tracks.size())) {
            const auto& track = tracks[(std::size_t)origin.trackIndex];
            base = synth::ui::resolveTrackColour(track.colourArgb, origin.trackIndex, track.muted);
        }

        const auto body = rect.toFloat().reduced(1.0f);
        g.setColour(base.withAlpha(kDragGhostFillAlpha));
        g.fillRoundedRectangle(body, 3.0f);
        g.setColour(base.brighter(0.4f).withAlpha(kDragGhostOutlineAlpha));
        g.drawRoundedRectangle(body, 3.0f, 1.0f);
        // Deliberately no name label: the label is the single strongest "this is a real clip"
        // cue, and its absence is what keeps a ghost legible AS a ghost at a glance.
    }
}

void TimelineClipLaneArea::paintDrawGhost(juce::Graphics& g) {
    const auto ghost = getDrawGhostRectForTest();
    if (ghost.isEmpty())
        return;
    g.setColour(juce::Colours::white.withAlpha(0.18f));
    g.fillRoundedRectangle(ghost.toFloat().reduced(1.0f), 3.0f);
    g.setColour(juce::Colours::white.withAlpha(0.75f));
    g.drawRoundedRectangle(ghost.toFloat().reduced(1.0f), 3.0f, 1.5f);
}

void TimelineClipLaneArea::paintSplitPreview(juce::Graphics& g) {
    if (!splitPreviewClip_.isValid() || doc_ == nullptr)
        return;
    const auto bounds = splitPreviewBounds(splitPreviewClip_, splitPreviewBeat_);
    if (bounds.isEmpty())
        return;

    const float x = (float)bounds.getCentreX();
    g.setColour(juce::Colours::white.withAlpha(0.9f));
    g.drawLine(x, (float)bounds.getY(), x, (float)bounds.getBottom(), 1.0f);
}

//==============================================================================
// Waveform painting (committed clips) and the live-recording strip.
//==============================================================================

const synth::PeaksFile::Data* TimelineClipLaneArea::findPeaksData(const juce::String& assetRef) {
    if (assetRef.isEmpty() || !peaksResolver_)
        return nullptr;

    auto it = peaksCache_.find(assetRef);
    if (it == peaksCache_.end()) {
        // A default-constructed Data (bucketSize == 0) is what a miss caches — see this method's
        // header comment for why that's deliberate rather than an oversight.
        synth::PeaksFile::Data data;
        const juce::File file = peaksResolver_(assetRef);
        if (file != juce::File())
            synth::PeaksFile::read(file, data);
        it = peaksCache_.emplace(assetRef, std::move(data)).first;
    }

    if (it->second.bucketSize <= 0 || it->second.numChannels <= 0 || it->second.buckets.empty())
        return nullptr;
    return &it->second;
}

void TimelineClipLaneArea::paintWaveform(juce::Graphics& g, const synth::Clip& clip, juce::Rectangle<int> rect) {
    if (rect.getWidth() <= kMinWidthForWaveform)
        return;
    const auto* data = findPeaksData(clip.assetRef);
    if (data == nullptr)
        return;

    double bpm = kFallbackBpm;
    double sampleRate = kFallbackSampleRate;
    if (transport_ != nullptr) {
        const auto snap = transport_->getPositionSnapshot();
        if (snap.bpm > 0.0)
            bpm = snap.bpm;
        if (snap.sampleRate > 0.0)
            sampleRate = snap.sampleRate;
    }

    const auto range = bucketRangeForClip(*data, clip.lengthBeats, clip.sourceStartSeconds, bpm, sampleRate);
    g.setColour(juce::Colours::black.withAlpha(0.4f));
    paintWaveformColumns(g, rect, data->buckets, data->numChannels, range.firstBucket, range.bucketCount);
}

bool TimelineClipLaneArea::assetExists(const juce::String& assetRef) {
    if (!assetExistsResolver_)
        return true; // no resolver installed: assume it exists — no placeholder without one

    auto it = assetExistsCache_.find(assetRef);
    if (it == assetExistsCache_.end())
        it = assetExistsCache_.emplace(assetRef, assetExistsResolver_(assetRef)).first;
    return it->second;
}

void TimelineClipLaneArea::paintMissingAssetPlaceholder(juce::Graphics& g, const synth::Clip& clip,
                                                        juce::Rectangle<int> rect) {
    const auto bounds = rect.reduced(1);
    if (bounds.getWidth() <= 0 || bounds.getHeight() <= 0)
        return;

    // Theme-token colours via the same dynamic_cast<AppLookAndFeel*> pattern getRowHeight() uses;
    // a hardcoded fallback (Theme::Colors::error's own default) when headless.
    juce::Colour hatchColour(0xffE5484D);
    if (auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel()))
        hatchColour = lf->getTheme().colors.error;

    // Dimmed fill first, so a missing clip visually recedes rather than reading as "just another
    // audio clip" — paintClip's own base fill/border are already drawn beneath this.
    g.setColour(juce::Colours::black.withAlpha(0.35f));
    g.fillRect(bounds);

    // Diagonal hatch, evenly spaced, clipped to the clip's own rect so the unbounded line family
    // never paints into neighbouring rows.
    {
        juce::Graphics::ScopedSaveState clipGuard(g);
        g.reduceClipRegion(bounds);
        g.setColour(hatchColour.withAlpha(0.55f));
        constexpr float kHatchSpacing = 8.0f;
        const float maxOffset = (float)(bounds.getWidth() + bounds.getHeight());
        for (float offset = -(float)bounds.getHeight(); offset < maxOffset; offset += kHatchSpacing) {
            const float x0 = (float)bounds.getX() + offset;
            g.drawLine(x0, (float)bounds.getBottom(), x0 + (float)bounds.getHeight(), (float)bounds.getY(), 1.0f);
        }
    }

    if (rect.getWidth() > kMinWidthForName) {
        const juce::String fileName = clip.assetRef.fromLastOccurrenceOf("/", false, false);
        g.setColour(juce::Colours::white.withAlpha(0.9f));
        g.setFont(juce::Font(10.0f));
        g.drawText("missing: " + fileName, rect.reduced(4, 2), juce::Justification::bottomLeft, true);
    }
}

void TimelineClipLaneArea::paintWaveformColumns(juce::Graphics& g, juce::Rectangle<int> rect,
                                                const std::vector<std::pair<float, float>>& buckets, int numChannels,
                                                int firstBucket, int bucketCount) {
    if (bucketCount <= 0 || numChannels <= 0)
        return;

    const auto bounds = rect.reduced(1);
    if (bounds.getWidth() <= 0 || bounds.getHeight() <= 0)
        return;

    const float midY = (float)bounds.getCentreY();
    const float halfHeight = (float)bounds.getHeight() * 0.5f;
    const int width = juce::jmax(1, bounds.getWidth());

    for (int x = bounds.getX(); x < bounds.getRight(); ++x) {
        const double frac = (double)(x - bounds.getX()) / (double)width;
        const int bucket = firstBucket + juce::jlimit(0, bucketCount - 1, (int)(frac * bucketCount));

        float minValue = 0.0f, maxValue = 0.0f;
        bool any = false;
        for (int channel = 0; channel < numChannels; ++channel) {
            const std::size_t index = (std::size_t)bucket * (std::size_t)numChannels + (std::size_t)channel;
            if (index >= buckets.size())
                continue;
            const auto& pair = buckets[index];
            minValue = any ? std::min(minValue, pair.first) : pair.first;
            maxValue = any ? std::max(maxValue, pair.second) : pair.second;
            any = true;
        }
        if (!any)
            continue;

        const float y0 = midY - juce::jlimit(-1.0f, 1.0f, maxValue) * halfHeight;
        const float y1 = midY - juce::jlimit(-1.0f, 1.0f, minValue) * halfHeight;
        g.drawLine((float)x, y0, (float)x, juce::jmax(y1, y0 + 1.0f), 1.0f);
    }
}

void TimelineClipLaneArea::clearLiveRecording() {
    if (!liveRecording_.active)
        return;
    ++liveStripRepaintCount_;
    repaint(liveStripRect_);
    liveRecording_ = {};
    livePeaks_.clear();
    liveStripRect_ = {};
}

void TimelineClipLaneArea::updateLiveRecording(const LiveRecordingInfo& info) {
    if (!info.active || info.tap == nullptr || doc_ == nullptr) {
        clearLiveRecording();
        return;
    }

    int trackIndex = -1;
    const auto& tracks = doc_->getTracks();
    for (int i = 0; i < (int)tracks.size(); ++i) {
        if (tracks[(std::size_t)i].id == info.track) {
            trackIndex = i;
            break;
        }
    }
    if (trackIndex < 0) {
        // The armed track vanished mid-take (deleted, or an undo/redo rebuilt the doc): nothing
        // sane to paint. Same handling as "nothing recording".
        clearLiveRecording();
        return;
    }

    std::vector<std::pair<float, float>> peaks;
    info.tap->copyLivePeaks(peaks);

    const bool wasActive = liveRecording_.active;
    const std::size_t previousBucketCount = livePeaks_.size();
    liveRecording_ = info;
    livePeaks_ = std::move(peaks);

    const int rowHeight = getRowHeight();
    const auto newRect = computeClipRect(viewState_, trackIndex, info.punchBeat,
                                         std::max(0.0, info.currentBeat - info.punchBeat), rowHeight);

    // Repaint-on-arrival: only when new peak buckets actually landed (or this is the strip's very
    // first frame) is a repaint issued — the transport tick alone (which moves the rect's right
    // edge every poll) is not "data arrival". The rect is still kept current either way, so a
    // LATER arrival's dirty-rect union covers however far the strip silently grew in between.
    if (wasActive && livePeaks_.size() == previousBucketCount) {
        liveStripRect_ = newRect;
        return;
    }

    const auto dirty = liveStripRect_.getUnion(newRect);
    liveStripRect_ = newRect;
    ++liveStripRepaintCount_;
    repaint(dirty);
}

void TimelineClipLaneArea::paintLiveRecordingStrip(juce::Graphics& g) {
    if (liveStripRect_.isEmpty())
        return;

    const auto bodyBounds = liveStripRect_.toFloat().reduced(1.0f);
    g.setColour(juce::Colours::white.withAlpha(0.12f)); // translucent — visibly "in progress",
                                                        // distinct from a committed clip's fill
    g.fillRoundedRectangle(bodyBounds, 3.0f);
    g.setColour(juce::Colours::white.withAlpha(0.4f));
    g.drawRoundedRectangle(bodyBounds, 3.0f, 1.0f);

    if (liveStripRect_.getWidth() <= kMinWidthForWaveform || livePeaks_.empty())
        return;

    // The master tap is always stereo — see RecordTapModule::kNumChannels and every
    // startCapture() call site in MainComponent — so this is not a guess specific to this class.
    const int numChannels = RecordTapModule::kNumChannels;
    const int bucketCount = (int)(livePeaks_.size() / (std::size_t)numChannels);
    g.setColour(juce::Colours::white.withAlpha(0.7f));
    paintWaveformColumns(g, liveStripRect_, livePeaks_, numChannels, 0, bucketCount);
}

//==============================================================================
void TimelineClipLaneArea::mouseDown(const juce::MouseEvent& e) {
    grabKeyboardFocus();
    dragMode_ = DragMode::None;
    pendingEmptyClick_ = false;

    if (e.mods.isPopupMenu()) {
        auto hit = hitTestClip(e.getPosition());
        if (!hit)
            return; // empty-space right-click: no menu, selection untouched (GraphEditor's rule)

        if (!selection_.contains(hit->id))
            selection_.setSelection({hit->id});
        repaint();
        showClipContextMenu(hit->id, e.getPosition());
        return;
    }

    if (!e.mods.isLeftButtonDown())
        return;

    // Every non-Select tool is a click action (Draw's drag included): none of them selects,
    // marquees or trims, so they never reach the pointer logic below.
    if (activeTool_ != EditTool::Select) {
        handleToolMouseDown(e);
        return;
    }

    auto hit = hitTestClip(e.getPosition());
    if (!hit) {
        mouseDownPos_ = e.getPosition();
        if (e.mods.isShiftDown())
            beginMarquee(e.getPosition(), true);
        else
            pendingEmptyClick_ = true; // deferred — see class comment
        return;
    }

    const bool additive = e.mods.isShiftDown() || e.mods.isCommandDown() || e.mods.isCtrlDown();
    if (additive) {
        selection_.toggle(hit->id);
        repaint();
        return; // a modifier-click edits the selection; it never begins a drag
    }

    // A plain click on an already-selected clip keeps the whole group intact so it can be dragged
    // together; clicking anything else collapses the selection onto it (ModuleComponent's rule).
    if (!selection_.contains(hit->id))
        selection_.setSelection({hit->id});

    mouseDownPos_ = e.getPosition();
    activeClip_ = hit->id;

    // hitTestClip() only ever returns a hit when doc_ is non-null, so doc_ is guaranteed live here.
    if (hit->zone == ClipHit::Zone::RightEdge) {
        if (const auto* clip = doc_->getClip(hit->id)) {
            dragMode_ = DragMode::ResizeRight;
            resizeOriginalStart_ = clip->startBeat;
            resizeOriginalLength_ = clip->lengthBeats;
            previewLength_ = resizeOriginalLength_;
        }
    } else if (hit->zone == ClipHit::Zone::LeftEdge) {
        if (const auto* clip = doc_->getClip(hit->id)) {
            dragMode_ = DragMode::ResizeLeft;
            resizeOriginalStart_ = clip->startBeat;
            resizeOriginalLength_ = clip->lengthBeats;
            previewStart_ = resizeOriginalStart_;
            previewLength_ = resizeOriginalLength_;
        }
    } else {
        // Move: a shared horizontal beat offset PLUS a shared track-row offset (see mouseDrag).
        // Snapshot every SELECTED clip's origin (not just the one grabbed) so a multi-selection
        // moves together by one delta rather than each clip snapping independently.
        //
        // Alt turns the whole gesture into a copy-drag: the originals stay exactly where they are
        // (in the doc AND on screen) and the release commits duplicates at the destination. There
        // is deliberately no Alt-click action — a copy of a clip on top of itself is not something
        // anyone asks for by clicking.
        dragMode_ = DragMode::Move;
        copyDrag_ = e.mods.isAltDown();
        dragClips_.clear();
        const auto& tracks = doc_->getTracks();
        for (auto id : selection_.getSelected()) {
            const auto* clip = doc_->getClip(id);
            if (clip == nullptr)
                continue;
            int trackIndex = 0;
            for (int i = 0; i < (int)tracks.size(); ++i)
                for (const auto& candidate : tracks[(std::size_t)i].clips)
                    if (candidate.id == id)
                        trackIndex = i;
            dragClips_.push_back({id, clip->startBeat, clip->lengthBeats, trackIndex});
        }
        previewDeltaBeats_ = 0.0;
        previewRowDelta_ = 0;
    }

    repaint();
}

void TimelineClipLaneArea::mouseDrag(const juce::MouseEvent& e) {
    if (dragMode_ == DragMode::Draw) {
        updateDrawGesture(e);
        return;
    }
    // Split/Glue/Erase/Mute already acted on the press; dragging one of them does nothing at all
    // (no marquee, no move) rather than something the icon never promised.
    if (activeTool_ != EditTool::Select)
        return;

    if (pendingEmptyClick_) {
        // A plain press on empty space that becomes a drag turns into a (non-additive) marquee —
        // there is no drag-to-pan gesture here (scrolling is wheel-only).
        pendingEmptyClick_ = false;
        beginMarquee(mouseDownPos_, false);
    }

    if (dragMode_ == DragMode::Marquee) {
        updateMarquee(e.getPosition());
        return;
    }

    if (dragMode_ == DragMode::None || doc_ == nullptr)
        return;

    const double deltaBeats = (double)(e.getPosition().x - mouseDownPos_.x) / viewState_.pixelsPerBeat;
    const double beatsPerBar = currentBeatsPerBar();

    if (dragMode_ == DragMode::Move) {
        double anchorOriginal = 0.0;
        for (const auto& origin : dragClips_)
            if (origin.id == activeClip_)
                anchorOriginal = origin.originalStart;

        const double snappedAnchorStart = viewState_.snapBeat(anchorOriginal + deltaBeats, beatsPerBar);
        double delta = snappedAnchorStart - anchorOriginal;

        // Clamp so no dragged clip's start goes negative — the whole group is held back together
        // rather than letting the front of the pack clip at 0 while the rest keep sliding.
        double minOriginal = 0.0;
        bool first = true;
        for (const auto& origin : dragClips_) {
            if (first || origin.originalStart < minOriginal)
                minOriginal = origin.originalStart;
            first = false;
        }
        delta = std::max(delta, -minOriginal);

        previewDeltaBeats_ = delta;

        // Vertical: one row delta for the WHOLE selection, legal only if every clip's destination
        // row exists and accepts its payload (TimelineDoc::moveClipToTrack's kind rule — an audio
        // clip onto an Audio row, a MIDI clip onto a Midi one, neither onto Automation). An
        // illegal drop clamps back to 0 — a same-lane move, i.e. exactly what this drag did before
        // it could cross tracks — rather than dropping the clips that would have fitted.
        const int rowHeight = getRowHeight();
        int rowDelta =
            rowHeight > 0 ? (int)std::llround((double)(e.getPosition().y - mouseDownPos_.y) / (double)rowHeight) : 0;
        if (rowDelta != 0) {
            const auto& tracks = doc_->getTracks();
            for (const auto& origin : dragClips_) {
                const int destRow = origin.trackIndex + rowDelta;
                const auto* clip = doc_->getClip(origin.id);
                if (clip == nullptr || !juce::isPositiveAndBelow(destRow, (int)tracks.size())) {
                    rowDelta = 0;
                    break;
                }
                const auto destKind = tracks[(std::size_t)destRow].kind;
                const auto neededKind = clip->assetRef.isNotEmpty() ? synth::TrackKind::Audio : synth::TrackKind::Midi;
                if (destKind != neededKind) {
                    rowDelta = 0;
                    break;
                }
            }
        }
        previewRowDelta_ = rowDelta;
    } else if (dragMode_ == DragMode::ResizeRight) {
        const double rawEnd = resizeOriginalStart_ + resizeOriginalLength_ + deltaBeats;
        const double snappedEnd = viewState_.snapBeat(rawEnd, beatsPerBar);
        previewLength_ = std::max(snappedEnd - resizeOriginalStart_, kMinClipLengthBeats);
    } else if (dragMode_ == DragMode::ResizeLeft) {
        const double end = resizeOriginalStart_ + resizeOriginalLength_;
        const double rawStart = resizeOriginalStart_ + deltaBeats;
        const double snappedStart = viewState_.snapBeat(rawStart, beatsPerBar);
        previewStart_ = juce::jlimit(0.0, end - kMinClipLengthBeats, snappedStart);
        previewLength_ = end - previewStart_;
    }

    repaint();
}

void TimelineClipLaneArea::mouseUp(const juce::MouseEvent& e) {
    if (dragMode_ == DragMode::Draw) {
        commitDrawGesture();
        return;
    }
    // The four click tools committed on the press; there is nothing left for the release to do
    // (and nothing of theirs to reset — they never entered a drag mode).
    if (activeTool_ != EditTool::Select)
        return;

    if (dragMode_ == DragMode::Marquee) {
        endMarquee();
        dragMode_ = DragMode::None;
        pendingEmptyClick_ = false;
        return;
    }

    if (pendingEmptyClick_) {
        pendingEmptyClick_ = false;
        if (!e.mods.isPopupMenu()) // right-click keeps the selection (GraphEditor's rule)
            selection_.clear();
        repaint();
        return;
    }

    if (doc_ != nullptr) {
        const bool moved = std::abs(previewDeltaBeats_) > 1e-9 || previewRowDelta_ != 0;
        if (dragMode_ == DragMode::Move && moved) {
            // Destination track ids are resolved BEFORE the mutation: track ids and their order
            // are stable across the clip moves below, but the clip vectors they hold are not.
            const auto clips = dragClips_;
            const double delta = previewDeltaBeats_;
            const int rowDelta = previewRowDelta_;
            const bool copying = copyDrag_;
            std::vector<synth::TrackId> destTracks;
            destTracks.reserve(clips.size());
            const auto& tracks = doc_->getTracks();
            for (const auto& origin : clips) {
                const int destRow = origin.trackIndex + rowDelta;
                destTracks.push_back(juce::isPositiveAndBelow(destRow, (int)tracks.size())
                                         ? tracks[(std::size_t)destRow].id
                                         : synth::TrackId{});
            }

            std::vector<synth::ClipId> newIds;
            auto mutate = [this, clips, destTracks, delta, copying, &newIds] {
                for (std::size_t i = 0; i < clips.size(); ++i) {
                    const auto& origin = clips[i];
                    const double newStart = origin.originalStart + delta;
                    if (!destTracks[i].isValid())
                        continue;
                    if (!copying) {
                        // moveClipToTrack onto the clip's OWN track is documented to behave
                        // exactly like moveClip, so the same-lane drag is unchanged by this path.
                        doc_->moveClipToTrack(origin.id, destTracks[i], newStart);
                        continue;
                    }
                    // duplicateClip drops the copy immediately after its source; the move is what
                    // puts it where the user actually dropped it.
                    const auto dup = doc_->duplicateClip(origin.id);
                    if (!dup.isValid())
                        continue;
                    doc_->moveClipToTrack(dup, destTracks[i], newStart);
                    newIds.push_back(dup);
                }
            };
            if (undoManager_)
                undoManager_->recordTimelineChange(*doc_, mutate);
            else
                mutate();

            // A copy-drag ends with the COPIES selected — the user's attention is on what they
            // just made, and the next drag should move it rather than the original.
            if (copying && !newIds.empty())
                selection_.setSelection(newIds);
        } else if (dragMode_ == DragMode::ResizeRight && std::abs(previewLength_ - resizeOriginalLength_) > 1e-9) {
            const auto id = activeClip_;
            const double newLength = previewLength_;
            auto mutate = [this, id, newLength] { doc_->resizeClip(id, newLength); };
            if (undoManager_)
                undoManager_->recordTimelineChange(*doc_, mutate);
            else
                mutate();
        } else if (dragMode_ == DragMode::ResizeLeft && std::abs(previewStart_ - resizeOriginalStart_) > 1e-9) {
            const auto id = activeClip_;
            const double newStart = previewStart_;
            const double newLength = previewLength_;
            // Two doc calls, ONE undo step: the left edge moves the clip's start (its
            // clip-relative notes travel with it — a divergence from per-note-anchored trimming,
            // deferred) and resizes it so the end stays fixed.
            auto mutate = [this, id, newStart, newLength] {
                doc_->moveClip(id, newStart);
                doc_->resizeClip(id, newLength);
            };
            if (undoManager_)
                undoManager_->recordTimelineChange(*doc_, mutate);
            else
                mutate();
        }
    }

    dragMode_ = DragMode::None;
    dragClips_.clear();
    previewDeltaBeats_ = 0.0;
    previewRowDelta_ = 0;
    copyDrag_ = false;
    repaint();
}

void TimelineClipLaneArea::mouseDoubleClick(const juce::MouseEvent& e) {
    // Authoring double-clicks belong to the pointer. With a tool active the first click already
    // did the tool's job (and Draw already created a clip), so a second one must not also open a
    // roll or a file chooser.
    if (activeTool_ != EditTool::Select)
        return;

    if (auto hit = hitTestClip(e.getPosition())) {
        if (onClipDoubleClicked)
            onClipDoubleClicked(hit->id);
        return;
    }

    // Empty lane space: author content on the row under the pointer (see mouseDoubleClick's
    // declaration for the per-kind contract).
    if (doc_ == nullptr)
        return;
    const auto row = trackIndexAt(e.getPosition());
    if (!row)
        return;

    const auto& track = doc_->getTracks()[(std::size_t)*row];
    const double startBeat = floorSnappedBeatAt(viewState_.xToBeat((double)e.getPosition().x));

    switch (track.kind) {
    case synth::TrackKind::Midi:
        createMidiClipAt(track.id, startBeat);
        break;
    case synth::TrackKind::Audio:
        requestAudioFileFor(track.id, startBeat);
        break;
    case synth::TrackKind::Automation:
        break; // no-op: an automation row's content is breakpoints, authored in the lane editor
    }
}

void TimelineClipLaneArea::createMidiClipAt(synth::TrackId track, double startBeat) {
    if (doc_ == nullptr)
        return;
    const auto* trackPtr = doc_->getTrack(track);
    if (trackPtr == nullptr)
        return;

    // One bar at the transport's current time signature (4 beats with no transport) — the same
    // beatsPerBar the Snap::Bar grid uses, so a bar-snapped clip fills exactly one grid cell.
    const double lengthBeats = currentBeatsPerBar();
    const juce::String name = "Clip " + juce::String((int)trackPtr->clips.size() + 1);

    synth::ClipId newId;
    auto mutate = [this, track, startBeat, lengthBeats, name, &newId] {
        newId = doc_->addClip(track, startBeat, lengthBeats, name);
    };
    if (undoManager_)
        undoManager_->recordTimelineChange(*doc_, mutate);
    else
        mutate();

    if (!newId.isValid())
        return; // rejected (the track is at kMaxClipsPerTrack): nothing to select or open

    selection_.setSelection({newId});
    repaint();
    // Straight into the note editor — the whole point of the gesture is that the user does not have
    // to find a second one to start drawing notes.
    if (onClipDoubleClicked)
        onClipDoubleClicked(newId);
}

void TimelineClipLaneArea::requestAudioFileFor(synth::TrackId track, double startBeat) {
    if (!audioFileChooser_ || !onAudioFileDropped)
        return;

    // The real chooser is async, so the callback may outlive this component (a theme reload or a
    // panel rebuild while the dialog is open) — SafePointer, not a raw `this`.
    juce::Component::SafePointer<TimelineClipLaneArea> safe(this);
    audioFileChooser_([safe, track, startBeat](const juce::File& file) {
        if (safe == nullptr || file == juce::File())
            return;
        if (safe->onAudioFileDropped)
            safe->onAudioFileDropped(track, startBeat, file);
    });
}

void TimelineClipLaneArea::launchAudioFileChooser(std::function<void(const juce::File&)> onChosen) {
    fileChooser_ = std::make_unique<juce::FileChooser>(
        "Add Audio Clip", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
        audioFormats_.getWildcardForAllFormats());
    fileChooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [onChosen = std::move(onChosen)](const juce::FileChooser& fc) {
                                  const auto file = fc.getResult();
                                  if (file != juce::File() && onChosen)
                                      onChosen(file);
                              });
}

//==============================================================================
bool TimelineClipLaneArea::isReadableAudioFile(const juce::File& file) const {
    return audioFormats_.findFormatForFileExtension(file.getFileExtension()) != nullptr;
}

juce::File TimelineClipLaneArea::firstAudioFileIn(const juce::StringArray& files) const {
    for (const auto& path : files) {
        const juce::File file(path);
        if (isReadableAudioFile(file))
            return file;
    }
    return {};
}

int TimelineClipLaneArea::dropRowFor(const juce::StringArray& files, int x, int y) const {
    if (doc_ == nullptr || firstAudioFileIn(files) == juce::File())
        return -1;
    const auto row = trackIndexAt({x, y});
    if (!row)
        return -1;
    // Audio rows only: a MIDI/Automation row cannot hold an asset, so it neither highlights nor
    // accepts a drop.
    return doc_->getTracks()[(std::size_t)*row].kind == synth::TrackKind::Audio ? *row : -1;
}

void TimelineClipLaneArea::setFileDropRow(int row) {
    if (row == fileDropRow_)
        return; // repaint ONLY on a row change — a drag reports every pixel of movement
    const int rowHeight = getRowHeight();
    const int previous = fileDropRow_;
    fileDropRow_ = row;
    if (previous >= 0)
        repaint(rowBounds(previous, rowHeight));
    if (fileDropRow_ >= 0)
        repaint(rowBounds(fileDropRow_, rowHeight));
}

bool TimelineClipLaneArea::isInterestedInFileDrag(const juce::StringArray& files) {
    return firstAudioFileIn(files) != juce::File();
}

void TimelineClipLaneArea::fileDragMove(const juce::StringArray& files, int x, int y) {
    setFileDropRow(dropRowFor(files, x, y));
}

void TimelineClipLaneArea::fileDragExit(const juce::StringArray& files) {
    juce::ignoreUnused(files);
    setFileDropRow(-1);
}

void TimelineClipLaneArea::filesDropped(const juce::StringArray& files, int x, int y) {
    const int row = dropRowFor(files, x, y);
    setFileDropRow(-1);
    if (row < 0 || doc_ == nullptr || !onAudioFileDropped)
        return;

    const auto file = firstAudioFileIn(files);
    if (file == juce::File())
        return;

    onAudioFileDropped(doc_->getTracks()[(std::size_t)row].id, floorSnappedBeatAt(viewState_.xToBeat((double)x)), file);
}

void TimelineClipLaneArea::mouseMove(const juce::MouseEvent& e) {
    if (activeTool_ != EditTool::Select) {
        // The tool cursor was set once, on the tool change / on entry — a move never rebuilds or
        // re-sets it. The only per-move work any tool does is the Split preview, and even that
        // only repaints when the snapped beat or the hovered clip actually changes.
        if (activeTool_ == EditTool::Split)
            updateSplitPreview(e.getPosition());
        return;
    }

    auto hit = hitTestClip(e.getPosition());
    if (hit && (hit->zone == ClipHit::Zone::LeftEdge || hit->zone == ClipHit::Zone::RightEdge))
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
    else
        setMouseCursor(juce::MouseCursor::NormalCursor);
}

void TimelineClipLaneArea::mouseEnter(const juce::MouseEvent&) { applyToolCursor(); }

void TimelineClipLaneArea::mouseExit(const juce::MouseEvent&) { clearToolPreviews(); }

void TimelineClipLaneArea::lookAndFeelChanged() {
    toolCursorsBuilt_ = false; // re-tinted icons -> different cursor images
    applyToolCursor();
}

//==============================================================================
std::optional<std::pair<double, double>> TimelineClipLaneArea::getSelectedClipSpan() const {
    if (doc_ == nullptr)
        return std::nullopt;

    bool any = false;
    double start = 0.0, end = 0.0;
    for (auto id : selection_.getSelected()) {
        const auto* clip = doc_->getClip(id);
        if (clip == nullptr)
            continue; // a selection entry the doc no longer has: skipped, never guessed at
        const double clipStart = clip->startBeat;
        const double clipEnd = clip->startBeat + clip->lengthBeats;
        start = any ? std::min(start, clipStart) : clipStart;
        end = any ? std::max(end, clipEnd) : clipEnd;
        any = true;
    }

    if (!any || !(end > start))
        return std::nullopt;
    return std::make_pair(start, end);
}

bool TimelineClipLaneArea::keyPressed(const juce::KeyPress& key) {
    // P = loop the selection (Cubase's locators-to-selection). Rebindable through
    // "timelineLoopSelection" but NOT a command: it is resolved right here, on the surface that
    // knows the span, so no selection (or no owner listening) returns false and the key keeps
    // whatever meaning it has elsewhere. With no ShortcutManager installed this is the hardcoded
    // bare P; with one installed an unset binding means no key at all (see setShortcutManager).
    const auto matchesLoopSelection = [this, &key] {
        const juce::KeyPress fallback('p', juce::ModifierKeys::noModifiers, 0);
        if (shortcuts_ == nullptr)
            return key == fallback;
        const auto binding = shortcuts_->getBinding("timelineLoopSelection");
        // keyPressMatches rather than == so a rebind onto a Shift-chorded symbol key survives the
        // macOS peer delivering the SHIFTED character as the key code.
        return ShortcutManager::keyPressMatches(binding, key);
    };

    if (matchesLoopSelection()) {
        const auto span = getSelectedClipSpan();
        if (!span || !onLoopRangeRequested)
            return false;
        onLoopRangeRequested(span->first, span->second);
        return true;
    }

    if (key == juce::KeyPress::escapeKey) {
        if (selection_.isEmpty())
            return false;
        selection_.clear();
        repaint();
        return true;
    }

    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey) {
        auto ids = selection_.getSelected();
        if (ids.empty() || doc_ == nullptr)
            return false;

        auto mutate = [this, ids] {
            for (auto id : ids)
                doc_->removeClip(id);
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

//==============================================================================
void TimelineClipLaneArea::beginMarquee(juce::Point<int> anchor, bool additive) {
    dragMode_ = DragMode::Marquee;
    marqueeAdditive_ = additive;
    marqueeAnchor_ = anchor;
    marqueeRect_ = juce::Rectangle<int>(anchor, anchor);
    marqueeBaseSelection_ = additive ? selection_.getSelected() : std::vector<synth::ClipId>{};

    // A non-additive marquee starts from nothing, so dragging over no clip deselects.
    if (!additive)
        selection_.clear();
    repaint();
}

void TimelineClipLaneArea::updateMarquee(juce::Point<int> current) {
    marqueeRect_ = juce::Rectangle<int>(marqueeAnchor_, current);
    auto hits = clipHitTestMarquee(marqueeRect_, collectClipRects());

    if (marqueeAdditive_) {
        std::set<synth::ClipId> merged(marqueeBaseSelection_.begin(), marqueeBaseSelection_.end());
        merged.insert(hits.begin(), hits.end());
        selection_.setSelection({merged.begin(), merged.end()});
    } else {
        selection_.setSelection(hits);
    }
    repaint();
}

void TimelineClipLaneArea::endMarquee() {
    marqueeRect_ = {};
    marqueeBaseSelection_.clear();
    marqueeAdditive_ = false;
    repaint();
}

//==============================================================================
// Edit tools: the active tool, its cursor, its gestures and its previews.
//==============================================================================

void TimelineClipLaneArea::setActiveTool(EditTool tool) {
    if (activeTool_ == tool)
        return;
    activeTool_ = tool;

    // A gesture in flight has no meaning under the new tool — cancel it (nothing is committed,
    // because every commit happens on mouseUp) rather than letting the next release apply the old
    // tool's action.
    dragMode_ = DragMode::None;
    dragClips_.clear();
    previewDeltaBeats_ = 0.0;
    previewRowDelta_ = 0;
    copyDrag_ = false;
    pendingEmptyClick_ = false;
    marqueeRect_ = {};
    clearToolPreviews();

    applyToolCursor();
    repaint();
}

void TimelineClipLaneArea::rebuildToolCursors() {
    auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());
    for (auto tool : kAllEditTools) {
        // getIcon returns nullptr in a headless build (no asset library linked in);
        // makeToolCursor is documented to fall back to a stock cursor for exactly that case.
        std::unique_ptr<juce::Drawable> icon = lf != nullptr ? lf->getIcon(iconForTool(tool)) : nullptr;
        toolCursors_[(std::size_t)tool] = makeToolCursor(tool, icon.get());
    }
    toolCursorsBuilt_ = true;
}

void TimelineClipLaneArea::applyToolCursor() {
    if (activeTool_ == EditTool::Select) {
        // The pointer's cursor is owned by mouseMove (edge zones show a resize cursor); resetting
        // it here just clears whatever tool cursor was showing.
        setMouseCursor(juce::MouseCursor::NormalCursor);
        return;
    }
    if (!toolCursorsBuilt_)
        rebuildToolCursors();
    setMouseCursor(toolCursors_[(std::size_t)activeTool_]);
}

void TimelineClipLaneArea::handleToolMouseDown(const juce::MouseEvent& e) {
    if (activeTool_ == EditTool::Draw) {
        beginDrawGesture(e);
        return;
    }

    // The other four act on a clip and only on a clip: a click on empty lane space with Split or
    // Erase held does nothing at all (it must not fall through to selection either — that would
    // make an "erase" click look like it selected something).
    auto hit = hitTestClip(e.getPosition());
    if (!hit)
        return;

    const double pointerBeat = viewState_.xToBeat((double)e.getPosition().x);
    switch (activeTool_) {
    case EditTool::Split:
        applyClipContextChoice(hit->id, ClipContextChoice::SplitAtPointer, pointerBeat);
        break;
    case EditTool::Glue:
        applyClipContextChoice(hit->id, ClipContextChoice::GlueWithNext, 0.0);
        break;
    case EditTool::Erase:
        applyClipContextChoice(hit->id, ClipContextChoice::Delete, 0.0);
        break;
    case EditTool::Mute:
        applyClipContextChoice(hit->id, ClipContextChoice::ToggleMute, 0.0);
        break;
    case EditTool::Select:
    case EditTool::Draw:
        break; // handled above / never reached
    }
}

synth::ClipId TimelineClipLaneArea::findGlueTarget(synth::ClipId id) const {
    if (doc_ == nullptr)
        return {};
    const auto* clip = doc_->getClip(id);
    const auto* track = doc_->getTrackForClip(id);
    if (clip == nullptr || track == nullptr)
        return {};

    const double end = clip->startBeat + clip->lengthBeats;
    synth::ClipId best;
    double bestStart = 0.0;
    for (const auto& candidate : track->clips) {
        if (candidate.id == id || candidate.startBeat < end - 1e-9)
            continue; // itself, anything before it, and anything overlapping it (joinClips refuses)
        if (!best.isValid() || candidate.startBeat < bestStart) {
            best = candidate.id;
            bestStart = candidate.startBeat;
        }
    }
    return best;
}

//---- Draw ---------------------------------------------------------------------

void TimelineClipLaneArea::beginDrawGesture(const juce::MouseEvent& e) {
    if (doc_ == nullptr)
        return;
    const auto row = trackIndexAt(e.getPosition());
    if (!row)
        return;

    const auto& track = doc_->getTracks()[(std::size_t)*row];
    if (track.kind != synth::TrackKind::Midi)
        return; // an audio row's content is an imported asset and an automation row's is
                // breakpoints — neither is something a pencil can draw

    dragMode_ = DragMode::Draw;
    drawTrack_ = track.id;
    drawRow_ = *row;
    drawAnchorBeat_ = floorSnappedBeatAt(viewState_.xToBeat((double)e.getPosition().x));
    drawEndBeat_ = drawAnchorBeat_;
    drawDragged_ = false;
}

void TimelineClipLaneArea::updateDrawGesture(const juce::MouseEvent& e) {
    // The end is snapped UP so a drag that has entered a cell always includes the whole cell, and
    // floored at one division so the smallest possible drag still makes a usable clip.
    const double raw = viewState_.xToBeat((double)e.getPosition().x);
    const double end = std::max(ceilSnappedBeatAt(raw), drawAnchorBeat_ + minDrawLengthBeats());
    if (drawDragged_ && std::abs(end - drawEndBeat_) < 1e-9)
        return; // same cell: no state change, so no repaint (the ghost is already correct)

    const auto before = getDrawGhostRectForTest();
    drawEndBeat_ = end;
    drawDragged_ = true;
    requestToolPreviewRepaint(before.getUnion(getDrawGhostRectForTest()));
}

void TimelineClipLaneArea::commitDrawGesture() {
    const auto ghost = getDrawGhostRectForTest();
    const bool dragged = drawDragged_ && drawEndBeat_ > drawAnchorBeat_ + 1e-9;
    const auto track = drawTrack_;
    const double anchor = drawAnchorBeat_;
    const double length = drawEndBeat_ - drawAnchorBeat_;

    dragMode_ = DragMode::None;
    drawDragged_ = false;
    drawRow_ = -1;
    drawTrack_ = {};
    if (!ghost.isEmpty())
        requestToolPreviewRepaint(ghost);

    if (doc_ == nullptr || !track.isValid())
        return;

    if (!dragged) {
        // A pencil CLICK is the same authoring gesture the empty-lane double-click already
        // performs — one bar, selected, straight into the note editor.
        createMidiClipAt(track, anchor);
        return;
    }

    const auto* trackPtr = doc_->getTrack(track);
    if (trackPtr == nullptr)
        return;
    const juce::String name = "Clip " + juce::String((int)trackPtr->clips.size() + 1);

    synth::ClipId newId;
    auto mutate = [this, track, anchor, length, name, &newId] { newId = doc_->addClip(track, anchor, length, name); };
    if (undoManager_)
        undoManager_->recordTimelineChange(*doc_, mutate);
    else
        mutate();

    if (!newId.isValid())
        return; // rejected (the track is at kMaxClipsPerTrack)
    selection_.setSelection({newId});
    repaint();
}

//---- Previews -----------------------------------------------------------------

void TimelineClipLaneArea::requestToolPreviewRepaint(juce::Rectangle<int> region) { repaint(region); }

juce::Rectangle<int> TimelineClipLaneArea::splitPreviewBounds(synth::ClipId clip, double beat) const {
    const auto rect = getClipRect(clip);
    if (rect.isEmpty())
        return {};
    const int x = (int)std::llround(viewState_.beatToX(beat));
    return {x - kSplitPreviewMarginPx, rect.getY(), 2 * kSplitPreviewMarginPx + 1, rect.getHeight()};
}

void TimelineClipLaneArea::updateSplitPreview(juce::Point<int> pos) {
    synth::ClipId clip;
    double beat = 0.0;
    if (doc_ != nullptr) {
        if (auto hit = hitTestClip(pos)) {
            if (const auto* c = doc_->getClip(hit->id)) {
                const double snapped = snappedBeatAt(viewState_.xToBeat((double)pos.x));
                // Only a split point strictly inside the clip is previewed — the same test
                // applyClipContextChoice performs before it splits, so the line is never drawn
                // where a click would do nothing.
                if (snapped > c->startBeat && snapped < c->startBeat + c->lengthBeats) {
                    clip = hit->id;
                    beat = snapped;
                }
            }
        }
    }

    if (clip == splitPreviewClip_ && (!clip.isValid() || std::abs(beat - splitPreviewBeat_) < 1e-9))
        return; // THE gate: pointer movement inside one snap cell repaints nothing

    const auto before =
        splitPreviewClip_.isValid() ? splitPreviewBounds(splitPreviewClip_, splitPreviewBeat_) : juce::Rectangle<int>();
    splitPreviewClip_ = clip;
    splitPreviewBeat_ = beat;
    const auto after = clip.isValid() ? splitPreviewBounds(clip, beat) : juce::Rectangle<int>();
    // ONE repaint per change, over the union — so "moved to the next beat" costs exactly one.
    requestToolPreviewRepaint(before.isEmpty() ? after : (after.isEmpty() ? before : before.getUnion(after)));
}

void TimelineClipLaneArea::clearToolPreviews() {
    if (!splitPreviewClip_.isValid())
        return;
    const auto before = splitPreviewBounds(splitPreviewClip_, splitPreviewBeat_);
    splitPreviewClip_ = {};
    splitPreviewBeat_ = 0.0;
    if (!before.isEmpty())
        requestToolPreviewRepaint(before);
}

std::optional<TimelineClipLaneArea::SplitPreview> TimelineClipLaneArea::getSplitPreviewForTest() const {
    if (!splitPreviewClip_.isValid())
        return std::nullopt;
    return SplitPreview{splitPreviewClip_, splitPreviewBeat_};
}

juce::Rectangle<int> TimelineClipLaneArea::getDrawGhostRectForTest() const {
    if (dragMode_ != DragMode::Draw || !drawDragged_ || drawRow_ < 0)
        return {};
    return computeClipRect(viewState_, drawRow_, drawAnchorBeat_, drawEndBeat_ - drawAnchorBeat_, getRowHeight());
}

//---- Inline rename ------------------------------------------------------------

void TimelineClipLaneArea::renameClip(synth::ClipId id, const juce::String& newName) {
    if (doc_ == nullptr)
        return;
    auto mutate = [this, id, newName] { doc_->setClipName(id, newName); };
    if (undoManager_)
        undoManager_->recordTimelineChange(*doc_, mutate);
    else
        mutate();
    repaint();
}

void TimelineClipLaneArea::beginRenameClip(synth::ClipId id) {
    if (doc_ == nullptr)
        return;

    // FIRST: any previous rename commits before a new one opens — and it may mutate the doc, so
    // nothing may hold a Clip pointer across it.
    finishRename(true);

    const auto* clip = doc_->getClip(id);
    if (clip == nullptr)
        return;
    const auto rect = getClipRect(id);
    if (rect.isEmpty())
        return;

    renamingClip_ = id;
    renameEditor_ = std::make_unique<juce::TextEditor>("clipRenameEditor");
    renameEditor_->setComponentID("timelineClipRenameEditor");
    renameEditor_->setMultiLine(false);
    renameEditor_->setReturnKeyStartsNewLine(false);
    renameEditor_->setText(clip->name, juce::dontSendNotification);
    renameEditor_->setBounds(rect.reduced(2).withHeight(std::min(20, std::max(12, rect.getHeight() - 4))));
    renameEditor_->onReturnKey = [this] { finishRename(true); };
    renameEditor_->onEscapeKey = [this] { finishRename(false); };
    // Clicking away is a commit, not a cancel — the same reading every in-place rename in this app
    // (and every DAW) has. Escape is the cancel.
    renameEditor_->onFocusLost = [this] { finishRename(true); };
    addAndMakeVisible(*renameEditor_);
    renameEditor_->selectAll();
    renameEditor_->grabKeyboardFocus();
}

void TimelineClipLaneArea::finishRename(bool commit) {
    if (renameEditor_ == nullptr)
        return;
    // Detach FIRST: deleting the editor takes focus away from it, which fires onFocusLost, which
    // re-enters here — and finds a null editor, so it stops.
    auto editor = std::move(renameEditor_);
    const auto id = renamingClip_;
    renamingClip_ = {};
    const juce::String text = editor->getText();
    editor.reset();

    if (commit)
        renameClip(id, text); // setClipName rejects a blank name, keeping the old one
}

//==============================================================================
void TimelineClipLaneArea::showClipContextMenu(synth::ClipId id, juce::Point<int> localPos) {
    if (doc_ == nullptr)
        return;
    const auto* clip = doc_->getClip(id);
    if (clip == nullptr)
        return;

    const double pointerBeat = viewState_.xToBeat((double)localPos.x);
    const double snappedPointer = snappedBeatAt(pointerBeat);
    const bool strictlyInside =
        snappedPointer > clip->startBeat && snappedPointer < clip->startBeat + clip->lengthBeats;

    juce::PopupMenu menu;
    juce::PopupMenu::Item split("Split at pointer");
    split.setEnabled(strictlyInside);
    split.action = [this, id, pointerBeat] {
        applyClipContextChoice(id, ClipContextChoice::SplitAtPointer, pointerBeat);
    };
    menu.addItem(split);

    // "Glue with next" is offered whenever a legal join target exists (see findGlueTarget) and
    // greyed out otherwise, rather than hidden — a menu whose items move around is harder to
    // learn than one whose items grey out.
    juce::PopupMenu::Item glue("Glue with next");
    const auto glueTarget = findGlueTarget(id);
    glue.setEnabled(glueTarget.isValid());
    glue.action = [this, id] { applyClipContextChoice(id, ClipContextChoice::GlueWithNext, 0.0); };
    menu.addItem(glue);

    menu.addItem("Duplicate", [this, id] { applyClipContextChoice(id, ClipContextChoice::Duplicate, 0.0); });
    // One toggling item rather than two, labelled for what the click will DO.
    menu.addItem(clip->muted ? "Unmute" : "Mute",
                 [this, id] { applyClipContextChoice(id, ClipContextChoice::ToggleMute, 0.0); });
    // Not a ClipContextChoice: renaming opens an editor rather than mutating, so the headless
    // seam is renameClip() and the enum case is inert (see ClipContextChoice's own comment).
    menu.addItem("Rename…", [this, id] { beginRenameClip(id); });
    menu.addItem("Delete", [this, id] { applyClipContextChoice(id, ClipContextChoice::Delete, 0.0); });

    // Offered for any audio clip (non-empty assetRef) regardless of whether the asset
    // currently resolves — relinking a PRESENT asset (pointing it at a different file) is just as
    // legitimate as fixing a missing one. A callback rather than a ClipContextChoice: relinking
    // needs a host FileChooser + AssetManager import this class doesn't have (see
    // onRelinkAudioRequested's own comment).
    if (clip->assetRef.isNotEmpty() && onRelinkAudioRequested) {
        menu.addSeparator();
        menu.addItem("Relink audio…", [this, id] { onRelinkAudioRequested(id); });
    }

    menu.showMenuAsync(juce::PopupMenu::Options());
}

void TimelineClipLaneArea::applyClipContextChoice(synth::ClipId id, ClipContextChoice choice, double pointerBeat) {
    if (doc_ == nullptr)
        return;

    switch (choice) {
    case ClipContextChoice::SplitAtPointer: {
        const auto* clip = doc_->getClip(id);
        if (clip == nullptr)
            return;
        const double snappedPointer = snappedBeatAt(pointerBeat);
        const double atBeat = snappedPointer - clip->startBeat; // clip-relative, per splitClip's contract
        if (!(atBeat > 0.0 && atBeat < clip->lengthBeats))
            return; // must land strictly inside

        auto mutate = [this, id, atBeat] { doc_->splitClip(id, atBeat); };
        if (undoManager_)
            undoManager_->recordTimelineChange(*doc_, mutate);
        else
            mutate();
        break;
    }
    case ClipContextChoice::Duplicate: {
        synth::ClipId newId;
        auto mutate = [this, id, &newId] { newId = doc_->duplicateClip(id); };
        if (undoManager_)
            undoManager_->recordTimelineChange(*doc_, mutate);
        else
            mutate();
        if (newId.isValid())
            selection_.setSelection({newId});
        break;
    }
    case ClipContextChoice::Delete: {
        auto mutate = [this, id] { doc_->removeClip(id); };
        if (undoManager_)
            undoManager_->recordTimelineChange(*doc_, mutate);
        else
            mutate();
        // The id cannot stay selected: a later batched move or Delete would name a freed clip.
        // (TimelinePanelComponent's refreshFromDoc prunes it too; this keeps a lane area driven
        // without a panel — every test, and the Erase tool — equally correct.)
        selection_.remove(id);
        break;
    }
    case ClipContextChoice::ToggleMute: {
        const auto* clip = doc_->getClip(id);
        if (clip == nullptr)
            return;
        const bool muted = !clip->muted;
        auto mutate = [this, id, muted] { doc_->setClipMuted(id, muted); };
        if (undoManager_)
            undoManager_->recordTimelineChange(*doc_, mutate);
        else
            mutate();
        break;
    }
    case ClipContextChoice::GlueWithNext: {
        const auto target = findGlueTarget(id);
        if (!target.isValid())
            return; // nothing after this clip: no mutation, and therefore no undo entry

        auto mutate = [this, id, target] { doc_->joinClips(id, target); };
        if (undoManager_)
            undoManager_->recordTimelineChange(*doc_, mutate);
        else
            mutate();
        // `target` was absorbed into `id` — the survivor keeps its own id, so only the swallowed
        // one has to leave the selection.
        selection_.remove(target);
        break;
    }
    case ClipContextChoice::Rename:
        // Deliberately inert: the rename UI has no headless meaning, and its commit path is
        // renameClip() (see ClipContextChoice). The case exists so the menu's whole vocabulary is
        // enumerable — a test can assert this choice mutates nothing.
        break;
    }

    repaint();
}

} // namespace synth::ui
