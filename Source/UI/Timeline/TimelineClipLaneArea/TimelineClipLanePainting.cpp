// TimelineClipLanePainting.cpp
//
// Tool affordances (copy-drag ghosts, Draw ghost, Split preview line) and waveform
// painting for committed clips, plus the live-recording strip. TimelineClipLaneArea is
// declared in TimelineClipLaneArea.h; sibling TimelineClipLane*.cpp files in this
// directory hold the rest of the class.

#include "TimelineClipLaneArea.h"
#include "TimelineClipLaneInternal.h"

#include "Modules/RecordTapModule.h"
#include "Transport/TransportService.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Timeline/TrackColour.h"
#include <algorithm>
#include <cmath>

namespace synth::ui {

namespace {
constexpr int kMinWidthForWaveform = 24;

// bpm/sampleRate fallbacks when there is no live transport (a headless test, or a
// TimelineClipLaneArea built with setTransport() never called) — the same "no transport, assume
// 120 bpm" convention currentBeatsPerBar() uses for beatsPerBar, and the same 44.1 kHz fallback
// MainComponent's own audio-take code uses when a snapshot's sampleRate is not yet known.
constexpr double kFallbackSampleRate = 44100.0;

// A copy-drag ghost: a translucent wash of the SOURCE track's colour, well under the 0.65/0.85 a
// real clip body paints at, plus a soft one-pixel outline. Translucency alone carries "not real
// yet" on purpose — a true blur would mean rendering the region to an image and filtering it once
// per drag frame, which is exactly the kind of unbounded per-frame paint work
// docs/layout/rendering.md rules out.
constexpr float kDragGhostFillAlpha = 0.4f;
constexpr float kDragGhostOutlineAlpha = 0.7f;
} // namespace

using namespace detail;

//==============================================================================
// Tool affordances: the copy-drag ghosts, the Draw ghost and the Split preview line. All three
// describe a result that does not exist yet, so all three are drawn well under a real clip's own
// alpha — the Draw ghost and the split line as bare outlines (they have no source to borrow a
// colour from), the copy-drag ghosts as a translucent wash of the source track's colour so the
// user can see WHICH clip each one came from mid-drag.
//==============================================================================

// The single geometry source shared by paintDragGhosts() and getDragGhostRectsForTest() —
// computing them separately is how a drawn affordance drifts from the one a test pins (the same
// reasoning GraphEditor::buildVisibleCables() states).
juce::Rectangle<int> TimelineClipLaneArea::dragGhostRectFor(const TimelineRowLayout& layout,
                                                            const DragOrigin& origin) const {
    return computeClipRect(viewState_, layout, origin.trackIndex + previewRowDelta_,
                           origin.originalStart + previewDeltaBeats_, origin.lengthBeats);
}

std::vector<juce::Rectangle<int>> TimelineClipLaneArea::getDragGhostRectsForTest() const {
    std::vector<juce::Rectangle<int>> rects;
    if (dragMode_ != DragMode::Move || !copyDrag_)
        return rects;
    const auto layout = getRowLayout();
    rects.reserve(dragClips_.size());
    for (const auto& origin : dragClips_)
        rects.push_back(dragGhostRectFor(layout, origin));
    return rects;
}

// The pair a copy-drag test asserts is UNCHANGED mid-drag, while getDragGhostRectsForTest() shows
// the delta.
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

    const auto layout = getRowLayout();
    const auto& tracks = doc_->getTracks();
    for (const auto& origin : dragClips_) {
        const auto rect = dragGhostRectFor(layout, origin);
        if (rect.getRight() < 0 || rect.getX() > getWidth())
            continue; // same offscreen cull paintClip() uses

        // The SOURCE track's colour, not the destination row's: a ghost that changed hue as the
        // pointer crossed rows would read as the clip having already landed there. Muted state
        // comes along for the same reason it does in paintClip() — a copy of a muted clip is
        // still muted when it lands.
        // Not a theme fallback (this is resolveTrackColour territory, which this file leaves
        // alone) — just a defensive default for the "should never happen" case of a source track
        // that vanished mid-drag, so an out-of-range index still paints something rather than
        // reading tracks[] out of bounds.
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

    // Themed via the same dynamic_cast<AppLookAndFeel*> idiom as paintFileDropHighlight below: a
    // ghost that has no clip/track of its own to borrow a colour from (see this section's header
    // comment) borrows the theme's accent instead — previously a flat white literal, which stayed
    // legible on Obsidian's dark bg0 by accident but would wash out on a light theme.
    const juce::Colour accent = synth::theme::themeOf(*this).colors.accent;

    g.setColour(accent.withAlpha(0.18f));
    g.fillRoundedRectangle(ghost.toFloat().reduced(1.0f), 3.0f);
    g.setColour(accent.withAlpha(0.75f));
    g.drawRoundedRectangle(ghost.toFloat().reduced(1.0f), 3.0f, 1.5f);
}

void TimelineClipLaneArea::paintSplitPreview(juce::Graphics& g) {
    if (!splitPreviewClip_.isValid() || doc_ == nullptr)
        return;
    const auto bounds = splitPreviewBounds(splitPreviewClip_, splitPreviewBeat_);
    if (bounds.isEmpty())
        return;

    // Themed the same way PianoRollComponent::paintSplitPreview is: the split line has no clip of
    // its own to borrow a colour from, so — like paintDrawGhost above — it borrows the theme's
    // accent instead of a flat white literal.
    const juce::Colour accent = synth::theme::themeOf(*this).colors.accent;

    const float x = (float)bounds.getCentreX();
    g.setColour(accent.withAlpha(0.9f));
    g.drawLine(x, (float)bounds.getY(), x, (float)bounds.getBottom(), 1.0f);
}

//==============================================================================
// Waveform painting (committed clips) and the live-recording strip.
//==============================================================================

// Returns nullptr for an empty ref, no resolver, an unresolvable file, or a file that fails
// synth::PeaksFile::read().
const synth::PeaksFile::Data* TimelineClipLaneArea::findPeaksData(const juce::String& assetRef) {
    if (assetRef.isEmpty() || !peaksResolver_)
        return nullptr;

    auto it = peaksCache_.find(assetRef);
    if (it == peaksCache_.end()) {
        // A default-constructed, structurally-invalid Data (bucketSize == 0) is what a miss caches
        // too, deliberately rather than by oversight, so a repeated paint of a still-missing asset
        // never re-touches disk; only invalidatePeaksCache()/refreshFromDoc() forget that.
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

// "Nothing resolves" covers no resolver set, an unresolvable ref, or an unreadable/absent peaks
// file.
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
    // Black at low alpha, not a theme token — a shadow-style darkening of whatever `fill` already
    // is (the module cards' drop shadows are composed the same way), not a colour of its own.
    g.setColour(juce::Colours::black.withAlpha(0.4f));
    paintWaveformColumns(g, rect, data->buckets, data->numChannels, range.firstBucket, range.bucketCount);
}

// Mirrors findPeaksData's cache shape exactly: a miss is cached too, so a still-missing clip never
// re-triggers the resolver on the next paint.
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
    const juce::Colour hatchColour = synth::theme::themeOf(*this).colors.error;

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

    // Same width threshold paintClip's own name label uses.
    if (rect.getWidth() > kMinWidthForName) {
        const juce::String fileName = clip.assetRef.fromLastOccurrenceOf("/", false, false);
        // Fixed white, not a theme token: it sits on the black-dimmed + hatched overlay painted
        // above (dark regardless of theme or track colour), unlike paintClip's own name label,
        // which is fixed black because ITS background is the undimmed, arbitrarily-hued fill.
        g.setColour(juce::Colours::white.withAlpha(0.9f));
        g.setFont(juce::Font(10.0f));
        g.drawText("missing: " + fileName, rect.reduced(4, 2), juce::Justification::bottomLeft, true);
    }
}

// Shared by paintWaveform() (a committed clip's peaks) and paintLiveRecordingStrip() (the live
// accumulator's peaks) — one juce::Graphics::drawLine per x column, sampling
// buckets[firstBucket + column's fraction of bucketCount] across every channel (min of mins, max
// of maxes — a simple downmix, deliberately kept cheap enough to run every paint).
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

// One repaint over wherever the strips used to be, then a clean reset.
void TimelineClipLaneArea::clearLiveRecording() { updateLiveRecordings({}); }

void TimelineClipLaneArea::updateLiveRecording(const LiveRecordingInfo& info) {
    if (!info.active) {
        updateLiveRecordings({});
        return;
    }
    updateLiveRecordings({info});
}

// MainComponent calls this every tick alongside the panel's other polled updates, whether or not anything is
// recording. Each take's peaks are copied (a lock held only for that copy, never on the audio thread). A take
// whose track vanished mid-take (deleted, or an undo rebuilt the doc) simply has no strip.
//
// Repaint-on-arrival: a strip repaints only when new peak buckets landed for it (or it is new, or gone); the
// transport tick alone, which moves its right edge every poll, is not data arrival. The rect is still kept
// current, so a later arrival's dirty union covers however far the strip silently grew in between. All the dirty
// areas of one tick go out as one repaint.
void TimelineClipLaneArea::updateLiveRecordings(const std::vector<LiveRecordingInfo>& takes) {
    std::vector<LiveStrip> next;
    if (doc_ != nullptr) {
        const auto& tracks = doc_->getTracks();
        for (const auto& info : takes) {
            if (!info.active || (info.tap == nullptr && !info.copyPeaks))
                continue;
            const auto found = std::find_if(tracks.begin(), tracks.end(),
                                            [&info](const synth::Track& track) { return track.id == info.track; });
            if (found == tracks.end())
                continue;
            LiveStrip strip;
            strip.info = info;
            if (info.copyPeaks)
                info.copyPeaks(strip.peaks);
            else
                info.tap->copyLivePeaks(strip.peaks);
            strip.rect = computeClipRect(viewState_, getRowLayout(), (int)(found - tracks.begin()), info.punchBeat,
                                         std::max(0.0, info.currentBeat - info.punchBeat));
            next.push_back(std::move(strip));
        }
    }

    juce::Rectangle<int> dirty;
    bool anyDirty = false;
    const auto addDirty = [&](juce::Rectangle<int> area) {
        dirty = anyDirty ? dirty.getUnion(area) : area;
        anyDirty = true;
    };
    for (std::size_t i = 0; i < next.size(); ++i) {
        const LiveStrip* previous =
            i < liveStrips_.size() && liveStrips_[i].info.track == next[i].info.track ? &liveStrips_[i] : nullptr;
        if (previous != nullptr && previous->peaks.size() == next[i].peaks.size())
            continue;
        addDirty(previous != nullptr ? previous->rect.getUnion(next[i].rect) : next[i].rect);
        if (previous == nullptr && i < liveStrips_.size())
            addDirty(liveStrips_[i].rect);
    }
    for (std::size_t i = next.size(); i < liveStrips_.size(); ++i)
        addDirty(liveStrips_[i].rect);

    liveStrips_ = std::move(next);
    if (anyDirty) {
        ++liveStripRepaintCount_;
        repaint(dirty);
    }
}

void TimelineClipLaneArea::paintLiveRecordingStrip(juce::Graphics& g, const LiveStrip& strip) {
    if (strip.rect.isEmpty())
        return;

    // Themed via the same accent idiom as paintDrawGhost/paintSplitPreview above: still
    // deliberately NOT the destination track's colour (kept distinct from a committed clip's
    // fill), but no flat white literal that could wash out against a light theme's background.
    const juce::Colour accent = synth::theme::themeOf(*this).colors.accent;

    const auto bodyBounds = strip.rect.toFloat().reduced(1.0f);
    g.setColour(accent.withAlpha(0.12f)); // translucent — visibly "in progress", distinct from a
                                          // committed clip's fill
    g.fillRoundedRectangle(bodyBounds, 3.0f);
    g.setColour(accent.withAlpha(0.4f));
    g.drawRoundedRectangle(bodyBounds, 3.0f, 1.0f);

    if (strip.rect.getWidth() <= kMinWidthForWaveform || strip.peaks.empty())
        return;

    // The take's own channel count: a whole-mix take is stereo, an input take mono or stereo.
    const int numChannels = std::max(1, strip.info.numChannels);
    const int bucketCount = (int)(strip.peaks.size() / (std::size_t)numChannels);
    g.setColour(accent.withAlpha(0.7f));
    paintWaveformColumns(g, strip.rect, strip.peaks, numChannels, 0, bucketCount);
}

// The lane editors paint their own rows over the extra areas; this only fills what shows through
// before they are placed (or past the last one), in the same bg-1 they use. The Automation track's
// row continues its header column's section background across the lanes.
void TimelineClipLaneArea::paintAutomationRows(juce::Graphics& g, const TimelineRowLayout& layout) {
    juce::Colour laneBg(0xff13161B), sectionBg(0xff232833);
    if (auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel())) {
        laneBg = lf->getTheme().colors.bg1;
        sectionBg = lf->getTheme().colors.surfaceHi;
    }
    const int scroll = (int)std::llround(viewState_.trackScrollY);
    const auto& tracks = doc_->getTracks();
    for (int i = 0; i < (int)tracks.size(); ++i) {
        const auto row = rowBounds(layout, i);
        if (tracks[(size_t)i].kind == synth::TrackKind::Automation) {
            g.setColour(sectionBg);
            g.fillRect(row);
        }
        if (const int extra = layout.trackExtraHeight(i); extra > 0) {
            g.setColour(laneBg);
            g.fillRect(0, layout.trackTop(i) + layout.trackRowHeight(i) - scroll, getWidth(), extra);
        }
    }
}

} // namespace synth::ui
