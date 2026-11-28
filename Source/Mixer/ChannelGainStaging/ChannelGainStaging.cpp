#include "ChannelGainStaging.h"

#include "AudioEngine/AudioEngine.h"
#include "Modules/ChannelStripModule.h"
#include "Transport/BounceExporter.h"
#include "Transport/StemExporter.h"
#include <algorithm>
#include <cmath>
#include <juce_audio_formats/juce_audio_formats.h>
#include <limits>
#include <map>

namespace synth::gainstaging {

namespace {

constexpr double kSilentDb = -std::numeric_limits<double>::infinity();
constexpr double kRenderTailSeconds = 0.5;

struct NoteSpan {
    double start = 0.0;
    double end = 0.0;
    double weight = 0.0;
};

struct Candidate {
    double start = 0.0; // the stretch's first beat
    double score = 0.0;
    double lead = 0.0; // beats before `start` where the render begins
};

std::vector<NoteSpan> noteSpansOf(const Track& track) {
    std::vector<NoteSpan> spans;
    if (track.kind != TrackKind::Midi || track.muted)
        return spans;
    for (const auto& clip : track.clips) {
        if (clip.muted)
            continue;
        const double clipEnd = clip.startBeat + clip.lengthBeats;
        for (const auto& note : clip.notes) {
            if (note.muted)
                continue;
            NoteSpan span;
            span.start = clip.startBeat + note.startBeat;
            span.end = std::min(clipEnd, span.start + std::max(note.lengthBeats, 0.0));
            span.weight = std::clamp(note.velocity, 1, 127) / 127.0;
            if (span.end > span.start)
                spans.push_back(span);
        }
    }
    return spans;
}

double overlapOf(const NoteSpan& span, double from, double to) {
    return std::max(0.0, std::min(span.end, to) - std::max(span.start, from));
}

// The stretches of one track's notes ranked by how much they play, best first, not overlapping each other.
std::vector<Candidate> bestStretches(const std::vector<NoteSpan>& spans, const WindowOptions& options) {
    std::set<double> starts;
    for (const auto& span : spans)
        starts.insert(std::floor(span.start));
    std::vector<Candidate> scored;
    for (const double start : starts) {
        Candidate candidate;
        candidate.start = start;
        double earliest = start;
        for (const auto& span : spans) {
            const double overlap = overlapOf(span, start, start + options.windowBeats);
            if (overlap <= 0.0)
                continue;
            candidate.score += span.weight * overlap;
            earliest = std::min(earliest, span.start);
        }
        candidate.lead = std::clamp(start - earliest, 0.0, options.maxLeadBeats);
        scored.push_back(candidate);
    }
    std::stable_sort(scored.begin(), scored.end(),
                     [](const Candidate& a, const Candidate& b) { return a.score > b.score; });
    std::vector<Candidate> chosen;
    for (const auto& candidate : scored) {
        if ((int)chosen.size() >= options.windowsPerTrack)
            break;
        const bool clashes = std::any_of(chosen.begin(), chosen.end(), [&](const Candidate& other) {
            return std::abs(other.start - candidate.start) < options.windowBeats;
        });
        if (!clashes && candidate.score > 0.0)
            chosen.push_back(candidate);
    }
    return chosen;
}

std::vector<BeatWindow> mergeWindows(std::vector<BeatWindow> windows, double gap) {
    std::sort(windows.begin(), windows.end(),
              [](const BeatWindow& a, const BeatWindow& b) { return a.startBeat < b.startBeat; });
    std::vector<BeatWindow> merged;
    for (const auto& window : windows) {
        if (!merged.empty() && window.startBeat - merged.back().endBeat <= gap)
            merged.back().endBeat = std::max(merged.back().endBeat, window.endBeat);
        else
            merged.push_back(window);
    }
    return merged;
}

double totalBeats(const std::vector<BeatWindow>& windows) {
    double total = 0.0;
    for (const auto& window : windows)
        total += window.endBeat - window.startBeat;
    return total;
}

double filePeakDb(const juce::File& file) {
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (reader == nullptr)
        return kSilentDb;
    constexpr int kChunk = 65536;
    float peak = 0.0f;
    juce::AudioBuffer<float> buffer((int)reader->numChannels, kChunk);
    for (juce::int64 pos = 0; pos < reader->lengthInSamples; pos += kChunk) {
        const int n = (int)std::min<juce::int64>(kChunk, reader->lengthInSamples - pos);
        reader->read(&buffer, 0, n, pos, true, true);
        peak = std::max(peak, buffer.getMagnitude(0, n));
    }
    return peak > 0.0f ? 20.0 * std::log10((double)peak) : kSilentDb;
}

double gainDbOf(juce::AudioProcessorGraph& graph, NodeID id) {
    auto* node = graph.getNodeForId(id);
    if (node == nullptr)
        return 0.0;
    for (auto* param : node->getProcessor()->getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(param);
            ranged != nullptr && ranged->paramID == "gain")
            return (double)ranged->convertFrom0to1(ranged->getValue());
    return 0.0;
}

} // namespace

std::vector<BeatWindow> planWindows(const TimelineDoc& doc, const WindowOptions& options) {
    // Cheap enough to hear the whole song: one window from the start to the last note's end.
    double songEnd = 0.0;
    for (const auto& track : doc.getTracks())
        for (const auto& span : noteSpansOf(track))
            songEnd = std::max(songEnd, span.end);
    if (songEnd > 0.0 && songEnd <= options.maxTotalBeats)
        return {{0.0, songEnd}};

    std::vector<std::vector<BeatWindow>> byRank((size_t)std::max(1, options.windowsPerTrack));
    for (const auto& track : doc.getTracks()) {
        const auto chosen = bestStretches(noteSpansOf(track), options);
        for (size_t rank = 0; rank < chosen.size() && rank < byRank.size(); ++rank)
            byRank[rank].push_back({chosen[rank].start - chosen[rank].lead, chosen[rank].start + options.windowBeats});
    }
    // The busiest stretch of every track is always rendered; the next ones only while the total stays within the cap.
    std::vector<BeatWindow> kept;
    for (const auto& rank : byRank) {
        auto trial = kept;
        trial.insert(trial.end(), rank.begin(), rank.end());
        trial = mergeWindows(trial, options.mergeGapBeats);
        if (kept.empty() || totalBeats(trial) <= options.maxTotalBeats)
            kept = trial;
        else
            break;
    }
    return kept;
}

std::set<NodeID> channelStripIds(const juce::AudioProcessorGraph& graph) {
    std::set<NodeID> ids;
    for (auto* node : graph.getNodes())
        if (dynamic_cast<ChannelStripModule*>(node->getProcessor()) != nullptr)
            ids.insert(node->nodeID);
    return ids;
}

std::vector<ChannelPeak> measureChannelPeaks(AudioEngine& engine, const std::vector<BeatWindow>& windows) {
    // The render plays the song, so automation lanes and modulation move parameters; put every one back afterwards so
    // measuring never changes the project.
    std::vector<std::pair<juce::AudioProcessorParameter*, float>> before;
    for (auto* node : engine.getGraph().getNodes())
        if (auto* processor = node->getProcessor())
            for (auto* param : processor->getParameters())
                before.emplace_back(param, param->getValue());
    struct Restore {
        std::vector<std::pair<juce::AudioProcessorParameter*, float>>& values;
        ~Restore() {
            for (auto& [param, value] : values)
                if (param->getValue() != value)
                    param->setValue(value);
        }
    } restore{before};

    std::map<NodeID, double> loudest;
    const auto folder = juce::File::getSpecialLocation(juce::File::tempDirectory)
                            .getChildFile("agentsynth-gain-staging-" + juce::Uuid().toString());
    for (size_t i = 0; i < windows.size(); ++i) {
        BounceOptions options;
        options.startBeat = windows[i].startBeat;
        options.endBeat = windows[i].endBeat;
        options.tailSeconds = kRenderTailSeconds;
        options.sampleRate = 44100.0;
        options.blockSize = 512;
        options.bitDepth = 32;
        options.numChannels = 2;
        const auto result = StemExporter::exportStems(engine, folder.getChildFile(juce::String((int)i)), options);
        if (!result.ok)
            continue;
        for (int s = 0; s < result.stemFiles.size() && s < (int)result.stemStrips.size(); ++s) {
            auto& peak = loudest.try_emplace(result.stemStrips[(size_t)s], kSilentDb).first->second;
            peak = std::max(peak, filePeakDb(result.stemFiles[s]));
        }
    }
    folder.deleteRecursively();
    std::vector<ChannelPeak> peaks;
    for (const auto& [strip, peak] : loudest)
        peaks.push_back({strip, peak});
    return peaks;
}

std::vector<Trim> planTrims(const std::vector<ChannelPeak>& peaks, const std::set<NodeID>& only,
                            const std::function<double(NodeID)>& currentGainDb, double targetPeakDb) {
    std::vector<Trim> trims;
    for (const auto& peak : peaks) {
        if (only.count(peak.strip) == 0 || !std::isfinite(peak.peakDb) || peak.peakDb <= targetPeakDb)
            continue;
        Trim trim;
        trim.strip = peak.strip;
        trim.peakDb = peak.peakDb;
        trim.fromDb = currentGainDb(peak.strip);
        const double wanted = trim.fromDb + (targetPeakDb - kTrimMarginDb - peak.peakDb);
        trim.toDb = std::max((double)ChannelStripModule::kMinGainDb, std::floor(wanted * 10.0) / 10.0);
        if (trim.toDb < trim.fromDb)
            trims.push_back(trim);
    }
    return trims;
}

std::vector<Trim> planChannelTrims(AudioEngine& engine, const TimelineDoc& doc, const std::set<NodeID>& only,
                                   double targetPeakDb, const WindowOptions& options) {
    if (only.empty())
        return {};
    const auto windows = planWindows(doc, options);
    if (windows.empty())
        return {};
    // Export Stems republishes the timeline before it renders; do the same so the render sees the clips just placed.
    engine.publishTimeline(doc);
    auto& graph = engine.getGraph();
    return planTrims(
        measureChannelPeaks(engine, windows), only, [&graph](NodeID id) { return gainDbOf(graph, id); }, targetPeakDb);
}

} // namespace synth::gainstaging
