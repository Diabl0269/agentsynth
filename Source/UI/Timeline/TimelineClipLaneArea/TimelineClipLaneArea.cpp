// TimelineClipLaneArea.cpp
//
// TimelineClipLaneArea's construction, TimelineDoc wiring, row/rect geometry and hit-testing,
// and the core paint() (clip bodies, empty-row hints, file-drop highlight, marquee).
// TimelineClipLaneArea is declared in TimelineClipLaneArea.h; sibling TimelineClipLane*.cpp
// files in this directory hold the rest of the class (tool-affordance/waveform painting,
// mouse handling, selection/marquee, edit tools).

#include "TimelineClipLaneArea.h"
#include "TimelineClipLaneInternal.h"

#include "Transport/TransportService.h"
#include "UI/Theme/AppLookAndFeel.h"
#include "UI/Timeline/TimelineTrackHeaderComponent.h"
#include "UI/Timeline/TrackColour.h"
#include <algorithm>
#include <cmath>

namespace synth::ui {

namespace {
constexpr int kMinWidthForNotePreview = 24;

// The empty-row hint (see TimelineClipLaneArea::emptyRowHintFor). ASCII only: these end up in a
// juce::String, whose `const char*` constructor decodes bytes as LATIN-1, so the "\xE2\x80\x94" em
// dash that used to sit here painted as three mojibake characters. An escape is no safer than a
// typed glyph — it is the same bytes. See the string-literal invariant in CLAUDE.md.
constexpr const char* kMidiEmptyRowHint = "Double-click to add a clip - or arm (R) and record";
constexpr const char* kAudioEmptyRowHint = "Drop an audio file - or arm (R) and record";

// The hint is dropped rather than clipped or shrunk below these: a row shorter than this has no
// room for an 11 px line, and one narrower than the text plus its padding would truncate mid-word.
constexpr int kMinRowHeightForHint = 24;
constexpr int kHintPaddingPx = 8;
constexpr float kHintFontHeight = 11.0f;

// A muted clip keeps its shape, its selection border and its waveform/notes, and loses only
// brightness — mute is reversible, so it must not look like damage. The name label dims further
// than the body (it is the one part a glance reads as "this clip is fine").
constexpr float kMutedClipLabelAlpha = 0.35f;
} // namespace

using namespace detail;

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
            if (pos.x <= rect.getX() + kResizeEdgeZonePx)
                hit.zone = ClipHit::Zone::LeftEdge;
            else if (pos.x >= rect.getRight() - kResizeEdgeZonePx)
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
        // Fixed white, not a theme token: these lines sit on `fill`, which is resolveTrackColour's
        // arbitrary per-track/per-user hue, not a theme colour — the contrast they need to read
        // against is unrelated to which app theme is active, only to that one clip's colour.
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
        // Fixed black, not a theme token, for the same reason as the note-preview lines above: the
        // label sits on `fill` (an arbitrary per-track hue), so its contrast need is independent of
        // which app theme is active.
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

    // SAME token recipe as GraphEditor's module-marquee band (GraphEditor.cpp
    // GraphEditor::Content::paint, "Marquee selection band", issue #156) and
    // PianoRollComponent::paintMarquee: accent fill at low alpha + a brighter accent border at the
    // theme's guideLineWidth. Previously a flat white at low alpha — indistinguishable from the
    // module marquee's own placeholder look before it too was themed, and nearly invisible on a
    // light theme. Keeping all three on one recipe means a theme change (or a user accent
    // override) moves all three marquees together rather than leaving one behind.
    auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());
    const juce::Colour accentColour = lf != nullptr ? lf->getTheme().colors.accent : juce::Colour(0xff00D1FF);
    const float lineWidth = lf != nullptr ? lf->getTheme().metrics.guideLineWidth : 1.5f;

    const auto bandF = marqueeRect_.toFloat();
    g.setColour(accentColour.withAlpha(0.12f));
    g.fillRect(bandF);
    g.setColour(accentColour.withAlpha(0.80f));
    g.drawRect(bandF, lineWidth);
}

} // namespace synth::ui
