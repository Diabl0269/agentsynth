// TimelineClipLaneWaveform.cpp
//
// How a clip's waveform is drawn: from the peaks sidecar (a peak shape in a deep shade of the clip with a denser RMS
// core), and, when a committed clip is zoomed in past one peak bucket per pixel, from the real samples on disk (exact
// columns, or the sample line itself). Also the live-recording strip's columns. TimelineClipLaneArea is declared in
// TimelineClipLaneArea.h; sibling TimelineClipLane*.cpp files in this directory hold the rest of the class.

#include "TimelineClipLaneArea.h"
#include "TimelineClipLaneInternal.h"
#include "WaveformSampleCache.h"

#include "UI/Timeline/TrackColour.h"

#include "Transport/TransportService.h"
#include <algorithm>
#include <cmath>

namespace synth::ui {

namespace {
// Below this many samples per pixel the wave is drawn as the sample line itself, not as per-column min/max.
constexpr double kSampleLineSamplesPerPixel = 2.0;
constexpr float kSampleLineWidth = 1.2f;
} // namespace

using namespace detail;

// The peak envelope is THE waveform, one solid shape in a deep shade of the clip's own colour (the way most DAWs draw
// a clip by default; docs/timeline/clips.md has the comparison); the RMS body is a denser core inside it, a step
// further along the same shade, so it reads as the loudness of one waveform rather than a second grey shape. The shade
// is the clip colour pushed toward black, or toward white on a very dark clip, until it reaches kWaveformMinContrast
// against the clip, so the wave reads on every track colour in every theme. Opaque, so the lane behind never changes
// it.
WaveformInk waveformInkFor(juce::Colour clipOnLane) {
    const juce::Colour clip = clipOnLane.withAlpha(1.0f);
    const juce::Colour target = colourLuminance(clip) > 0.12f ? juce::Colours::black : juce::Colours::white;
    // The lightest shade that reads, so the core still has room to go a step further on a saturated clip.
    juce::Colour peak = clip.interpolatedWith(target, 0.3f);
    for (float t = 0.35f; t <= 1.0f && colourContrast(peak, clip) < kWaveformMinContrast; t += 0.05f)
        peak = clip.interpolatedWith(target, t);
    juce::Colour body = peak.interpolatedWith(target, 0.45f);
    for (float t = 0.55f; t <= 1.0f && colourContrast(body, peak) < kWaveformCoreStep; t += 0.1f)
        body = peak.interpolatedWith(target, t);
    return {peak, body, peak};
}

// "Nothing resolves" covers no resolver set, an unresolvable ref, or an unreadable/absent peaks file.
void TimelineClipLaneArea::paintWaveform(juce::Graphics& g, const synth::Clip& clip, juce::Rectangle<int> rect,
                                         juce::Colour onLane) {
    lastWaveformSource_ = WaveformSource::None;
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

    const WaveformInk ink = waveformInkFor(onLane);
    g.setColour(ink.flat);
    if (rect.getWidth() <= kMinWidthForWaveform) {
        lastWaveformSource_ = WaveformSource::Peaks;
        const auto range = bucketRangeForClip(*data, clip.lengthBeats, clip.sourceStartSeconds, bpm, sampleRate);
        paintWaveformSummary(g, rect, *data, range.firstBucket, range.bucketCount);
        return;
    }
    if (paintWaveformFromSamples(g, clip, rect, *data, bpm, sampleRate, ink))
        return;
    lastWaveformSource_ = WaveformSource::Peaks;
    const auto range = bucketRangeForClip(*data, clip.lengthBeats, clip.sourceStartSeconds, bpm, sampleRate);
    paintWaveformColumns(g, rect, data->buckets, data->rms, data->numChannels, range.firstBucket, range.bucketCount,
                         ink);
}

// What paintWaveform draws for `clip` in a rect `width` wide: nothing without peaks, a one-bar summary below
// kMinWidthForWaveform, else the per-column waveform. Read by the test-mode state and the tests.
TimelineClipLaneArea::WaveformShape TimelineClipLaneArea::waveformShapeFor(const synth::Clip& clip, int width) {
    if (findPeaksData(clip.assetRef) == nullptr)
        return WaveformShape::None;
    return width <= kMinWidthForWaveform ? WaveformShape::Summary : WaveformShape::Columns;
}

// A clip too narrow for columns still shows its audio has a waveform: one centred bar across the clip, as tall as the
// loudest peak under it (at least two pixels, so a quiet clip is not lost).
void TimelineClipLaneArea::paintWaveformSummary(juce::Graphics& g, juce::Rectangle<int> rect,
                                                const synth::PeaksFile::Data& data, int firstBucket, int bucketCount) {
    if (bucketCount <= 0)
        return;
    float peak = 0.0f;
    const std::size_t first = (std::size_t)firstBucket * (std::size_t)data.numChannels;
    const std::size_t last =
        std::min(data.buckets.size(), first + (std::size_t)bucketCount * (std::size_t)data.numChannels);
    for (std::size_t i = first; i < last; ++i)
        peak = std::max({peak, std::abs(data.buckets[i].first), std::abs(data.buckets[i].second)});

    const auto bounds = rect.reduced(0, 1);
    if (bounds.getWidth() <= 0 || bounds.getHeight() <= 0)
        return;
    const int height = juce::jlimit(2, bounds.getHeight(),
                                    juce::roundToInt(juce::jlimit(0.0f, 1.0f, peak) * (float)bounds.getHeight()));
    g.fillRect(bounds.getX(), bounds.getCentreY() - height / 2, bounds.getWidth(), height);
}

// Shared by paintWaveform() (a committed clip's peaks) and paintLiveRecordingStrip() (the live accumulator's peaks):
// per x column, the min of mins / max of maxes of the buckets that column covers, so zooming out never skips a loud
// bucket, and with fewer buckets than columns the line between bucket centres (smoothToColumns), so zooming in never
// shows steps.
void TimelineClipLaneArea::paintWaveformColumns(juce::Graphics& g, juce::Rectangle<int> rect,
                                                const std::vector<std::pair<float, float>>& buckets,
                                                const std::vector<float>& rms, int numChannels, int firstBucket,
                                                int bucketCount, const WaveformInk& ink) {
    const auto bounds = rect.reduced(1);
    if (bounds.getWidth() <= 0 || bounds.getHeight() <= 0)
        return;

    const auto columns =
        synth::PeaksFile::smoothToColumns(buckets, numChannels, firstBucket, bucketCount, bounds.getWidth());
    const auto bodies =
        rms.size() == buckets.size()
            ? synth::PeaksFile::smoothRmsToColumns(rms, numChannels, firstBucket, bucketCount, bounds.getWidth())
            : std::vector<float>{};
    fillWaveformColumns(g, bounds, 0, columns, bodies, ink);
}

// Two filled anti-aliased paths, so the wave is smooth on a 2x display: the peak shape (top edge = max, bottom edge =
// min) in `ink.outline` and, inside it, the RMS core (+-rms clamped to the peak shape) in `ink.body`. Without `bodies`
// one outline path in `ink.flat`. An outline thinner than a pixel is widened to one so silence still shows a hairline.
void fillWaveformColumns(juce::Graphics& g, juce::Rectangle<int> bounds, int firstColumn,
                         const std::vector<std::pair<float, float>>& columns, const std::vector<float>& bodies,
                         const WaveformInk& ink) {
    if (columns.empty())
        return;
    const float midY = (float)bounds.getCentreY();
    const float halfHeight = (float)bounds.getHeight() * 0.5f;
    const auto yFor = [&](float v) { return midY - juce::jlimit(-1.0f, 1.0f, v) * halfHeight; };
    const bool hasBody = bodies.size() == columns.size();

    const float xLeft = (float)(bounds.getX() + firstColumn);
    const float xRight = xLeft + (float)columns.size();
    std::vector<float> outTop(columns.size()), outBottom(columns.size()), bodyTop(columns.size()),
        bodyBottom(columns.size());
    for (std::size_t i = 0; i < columns.size(); ++i) {
        const float lo = std::min(columns[i].first, columns[i].second);
        const float hi = std::max(columns[i].first, columns[i].second);
        float y0 = yFor(hi);
        float y1 = yFor(lo);
        if (y1 - y0 < 1.0f) {
            const float mid = (y0 + y1) * 0.5f;
            y0 = mid - 0.5f;
            y1 = mid + 0.5f;
        }
        outTop[i] = y0;
        outBottom[i] = y1;
        if (hasBody) {
            const float r = juce::jlimit(0.0f, 1.0f, bodies[i]);
            bodyTop[i] = yFor(std::min(hi, r));
            bodyBottom[i] = std::max(yFor(std::max(lo, -r)), bodyTop[i]);
        }
    }

    const auto buildPath = [&](const std::vector<float>& top, const std::vector<float>& bottom) {
        juce::Path path;
        const std::size_t n = top.size();
        path.startNewSubPath(xLeft, top[0]);
        for (std::size_t i = 0; i < n; ++i)
            path.lineTo(xLeft + (float)i + 0.5f, top[i]);
        path.lineTo(xRight, top[n - 1]);
        path.lineTo(xRight, bottom[n - 1]);
        for (std::size_t i = n; i-- > 0;)
            path.lineTo(xLeft + (float)i + 0.5f, bottom[i]);
        path.lineTo(xLeft, bottom[0]);
        path.closeSubPath();
        return path;
    };

    g.setColour(hasBody ? ink.outline : ink.flat);
    g.fillPath(buildPath(outTop, outBottom));
    if (hasBody) {
        g.setColour(ink.body);
        g.fillPath(buildPath(bodyTop, bodyBottom));
    }
}

// Zoomed in past one peak bucket per pixel (fewer than bucketSize samples per pixel) a COMMITTED clip draws its real
// samples: exact per-column min/max with the RMS body, or from under kSampleLineSamplesPerPixel the sample line
// itself. Channels fold the way peaks do: min of mins and max of maxes per column; the line follows, per sample, the
// channel with the largest magnitude. Never used for a live take (its file is still being written; the live strip
// does not come through here) and never when the asset is missing or its read fails; paintWaveform then draws the
// interpolated peaks instead. Only the columns inside the painter's clip rectangle are derived, from a cached block.
bool TimelineClipLaneArea::paintWaveformFromSamples(juce::Graphics& g, const synth::Clip& clip,
                                                    juce::Rectangle<int> rect, const synth::PeaksFile::Data& data,
                                                    double bpm, double transportRate, const WaveformInk& ink) {
    const auto bounds = rect.reduced(1);
    if (!sampleCache_->hasResolver() || bounds.getWidth() <= 0 || bounds.getHeight() <= 0 ||
        !sampleCache_->canRead(clip.assetRef) || !assetExists(clip.assetRef))
        return false;

    const double fileRate = data.sampleRate > 0.0 ? data.sampleRate : transportRate;
    const auto span = synth::ui::sampleSpanForClip(fileRate, clip.sourceStartSeconds, clip.lengthBeats, bpm);
    const double samplesPerPixel = span.numSamples / (double)bounds.getWidth();
    if (samplesPerPixel <= 0.0 || samplesPerPixel >= (double)data.bucketSize)
        return false;

    const auto clipBounds = g.getClipBounds();
    const int xLo = std::max(bounds.getX(), clipBounds.getX());
    const int xHi = std::min(bounds.getRight(), clipBounds.getRight());
    if (xHi <= xLo) {
        lastWaveformSource_ = WaveformSource::SampleColumns; // nothing of it is on screen
        return true;
    }
    const int firstColumn = xLo - bounds.getX();
    const int numColumns = xHi - xLo;
    const double visibleStart = span.startSample + (double)firstColumn * samplesPerPixel;
    const double visibleEnd = visibleStart + (double)numColumns * samplesPerPixel;
    const std::int64_t needStart = std::max<std::int64_t>(0, (std::int64_t)std::floor(visibleStart) - 1);
    const std::int64_t needEnd = (std::int64_t)std::ceil(visibleEnd) + 2;
    if (needEnd - needStart > WaveformSampleCache::kMaxBlockFrames)
        return false;

    std::int64_t blockStart = 0;
    const auto* block = sampleCache_->find(clip.assetRef, needStart, (int)(needEnd - needStart), blockStart);
    if (block == nullptr)
        return false;

    if (samplesPerPixel >= kSampleLineSamplesPerPixel) {
        const auto columns =
            synth::PeaksFile::sampleColumns(*block, blockStart, visibleStart, samplesPerPixel, numColumns);
        fillWaveformColumns(g, bounds, firstColumn, columns.minMax, columns.rms, ink);
        lastWaveformSource_ = WaveformSource::SampleColumns;
        return true;
    }

    const float midY = (float)bounds.getCentreY();
    const float halfHeight = (float)bounds.getHeight() * 0.5f;
    juce::Path line;
    const int channels = block->getNumChannels();
    const std::int64_t last = std::min<std::int64_t>(needEnd, blockStart + block->getNumSamples() - 1);
    for (std::int64_t i = needStart; i <= last; ++i) {
        float v = 0.0f;
        for (int ch = 0; ch < channels; ++ch) {
            const float s = block->getReadPointer(ch)[i - blockStart];
            if (std::abs(s) > std::abs(v))
                v = s;
        }
        const float x = (float)bounds.getX() + (float)(((double)i - span.startSample) / samplesPerPixel);
        const float y = midY - juce::jlimit(-1.0f, 1.0f, v) * halfHeight;
        if (i == needStart)
            line.startNewSubPath(x, y);
        else
            line.lineTo(x, y);
    }
    juce::Graphics::ScopedSaveState guard(g);
    g.reduceClipRegion(bounds);
    g.setColour(ink.outline);
    g.strokePath(line,
                 juce::PathStrokeType(kSampleLineWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    lastWaveformSource_ = WaveformSource::SampleLine;
    return true;
}

} // namespace synth::ui
