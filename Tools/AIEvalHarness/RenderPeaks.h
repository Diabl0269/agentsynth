/*
    RenderPeaks.h -- AIEvalHarness `--render-peaks <bundle or directory> [--json FILE]`: render saved
    projects offline and report their loudest sample.

    Each bundle is opened in a headless MainComponent through openProjectForTest, then bounced with
    synth::BounceExporter (master mix) and synth::StemExporter (one file per mixer channel, named after
    its track) over the whole arrangement plus a short tail. Renders are 32-bit float WAV so a mix
    that goes over full scale is measured, not clipped by the file format.
*/
#pragma once

#include "AudioEngine/AudioEngine.h"
#include "MainComponent/MainComponent.h"
#include "ProjectBundle.h"
#include "SaveProjects.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "Transport/BounceExporter.h"
#include "Transport/StemExporter.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>

namespace render_peaks {

struct TrackPeak {
    juce::String name;
    double peakDb = 0.0;
};

struct BundlePeaks {
    juce::String bundle;
    double masterPeakDb = 0.0;
    std::vector<TrackPeak> tracks;
    juce::String stemNote; // why there are no per-track peaks, when there are none
    bool clips() const { return masterPeakDb >= 0.0; }
};

constexpr double kTailSeconds = 2.0;
constexpr double kNoSignalDb = -std::numeric_limits<double>::infinity();

// 20*log10 of the largest absolute sample in the file; -inf for silence. Sets `error` when unreadable.
inline double filePeakDb(const juce::File& file, juce::String& error) {
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (reader == nullptr) {
        error = "cannot read " + file.getFileName();
        return kNoSignalDb;
    }
    float peak = 0.0f;
    constexpr int kChunk = 65536;
    juce::AudioBuffer<float> buffer((int)reader->numChannels, kChunk);
    for (juce::int64 pos = 0; pos < reader->lengthInSamples; pos += kChunk) {
        const int n = (int)std::min<juce::int64>(kChunk, reader->lengthInSamples - pos);
        reader->read(&buffer, 0, n, pos, true, true);
        peak = std::max(peak, buffer.getMagnitude(0, n));
    }
    return peak > 0.0f ? 20.0 * std::log10((double)peak) : kNoSignalDb;
}

// "03 - Dark Bell.wav" -> "Dark Bell" (StemSession's file naming).
inline juce::String trackNameFromStem(const juce::File& stem) {
    const auto base = stem.getFileNameWithoutExtension();
    const int dash = base.indexOf(" - ");
    return dash >= 0 ? base.substring(dash + 3) : base;
}

// The options the app's Export Audio dialog would build for "whole arrangement": beat 0 to the end of the
// last clip, at the transport's tempo, plus the harness tail. Float WAV keeps overs measurable.
inline synth::BounceOptions wholeProjectOptions(double endBeat) {
    synth::BounceOptions options;
    options.startBeat = 0.0;
    options.endBeat = endBeat;
    options.tailSeconds = kTailSeconds;
    options.sampleRate = 44100.0;
    options.blockSize = 512;
    options.bitDepth = 32;
    options.numChannels = 2;
    options.format = synth::BounceFormat::Wav;
    return options;
}

// Opens `bundle`, renders master and stems into a folder under `scratch`, and measures them. Returns false
// with `error` set when the bundle does not open or the master render fails; a stem failure only leaves
// `out.stemNote` (the master number is still the answer to "is it too loud").
inline bool renderBundle(const juce::File& scratch, const juce::File& bundle, BundlePeaks& out, juce::String& error) {
    out.bundle = bundle.getFileName();
    MainComponent mc(std::make_unique<save_projects::NullProvider>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    if (!mc.openProjectForTest(bundle)) {
        error = "the project did not open";
        return false;
    }
    // The app republishes the timeline right before every export; do the same so the render sees the clips.
    mc.getAudioEngine().publishTimeline(mc.getTimelineDoc());

    const double endBeat = mc.getTimelineDoc().getArrangementEndBeat();
    // An empty arrangement still renders (one bar of silence) so it reports -inf rather than failing.
    const auto options = wholeProjectOptions(endBeat > 0.0 ? endBeat : 4.0);
    const auto dir = scratch.getNonexistentChildFile(bundle.getFileNameWithoutExtension(), "", false);
    dir.createDirectory();

    const auto master = dir.getChildFile("master.wav");
    const auto bounced = synth::BounceExporter::bounce(mc.getAudioEngine(), master, options);
    if (!bounced.ok) {
        error = "master render failed: " + bounced.message;
        return false;
    }
    out.masterPeakDb = filePeakDb(master, error);
    if (error.isNotEmpty())
        return false;

    if (!synth::StemExporter::hasChannelStrips(mc.getAudioEngine())) {
        out.stemNote = "no mixer channels, master only";
        return true;
    }
    const auto stems = synth::StemExporter::exportStems(mc.getAudioEngine(), dir.getChildFile("stems"), options, {},
                                                        &mc.getTimelineDoc());
    if (!stems.ok) {
        out.stemNote = "stem render failed (" + stems.message + "), master only";
        return true;
    }
    for (const auto& stem : stems.stemFiles) {
        juce::String stemError;
        const double db = filePeakDb(stem, stemError);
        if (stemError.isNotEmpty()) {
            out.stemNote = stemError;
            continue;
        }
        out.tracks.push_back({trackNameFromStem(stem), db});
    }
    return true;
}

inline juce::String dbText(double db) { return std::isinf(db) ? juce::String("-inf") : juce::String(db, 1); }

inline juce::String lineFor(const BundlePeaks& p) {
    juce::String line = p.bundle + ": master peak " + dbText(p.masterPeakDb) + " dBFS";
    for (const auto& t : p.tracks)
        line += ", " + t.name + " " + dbText(t.peakDb) + " dBFS";
    if (p.stemNote.isNotEmpty())
        line += " (" + p.stemNote + ")";
    return p.clips() ? line + " CLIPS" : line;
}

// -inf is not valid JSON; a silent file is written as null.
inline juce::var dbVar(double db) { return std::isinf(db) ? juce::var() : juce::var(db); }

inline void writeJson(const juce::File& file, const std::vector<BundlePeaks>& all) {
    juce::Array<juce::var> records;
    for (const auto& p : all) {
        auto* rec = new juce::DynamicObject();
        rec->setProperty("bundle", p.bundle);
        rec->setProperty("masterPeakDb", dbVar(p.masterPeakDb));
        juce::Array<juce::var> tracks;
        for (const auto& t : p.tracks) {
            auto* tr = new juce::DynamicObject();
            tr->setProperty("name", t.name);
            tr->setProperty("peakDb", dbVar(t.peakDb));
            tracks.add(juce::var(tr));
        }
        rec->setProperty("tracks", tracks);
        rec->setProperty("clips", p.clips());
        records.add(juce::var(rec));
    }
    file.replaceWithText(juce::JSON::toString(juce::var(records)));
}

// `--render-peaks <bundle or directory>`: one line per bundle and a summary. A directory means every
// *.agsproj directly inside it, sorted by name. Exit 1 when a bundle fails to open or render.
inline int run(const save_projects::Environment& env, const juce::File& target, const juce::File& jsonOut) {
    juce::Array<juce::File> bundles;
    if (target.isDirectory() && !synth::ProjectBundle::isBundle(target))
        bundles = target.findChildFiles(juce::File::findDirectories, false,
                                        "*" + juce::String(synth::ProjectBundle::kBundleExtension));
    else
        bundles.add(target);
    if (bundles.isEmpty()) {
        std::fprintf(stderr, "%s: no .agsproj bundles found\n", target.getFullPathName().toRawUTF8());
        return 1;
    }
    std::sort(bundles.begin(), bundles.end(),
              [](const juce::File& a, const juce::File& b) { return a.getFileName() < b.getFileName(); });

    const auto scratch = env.scratchDir().getChildFile("render-peaks");
    scratch.createDirectory();
    std::vector<BundlePeaks> all;
    int failed = 0, clipping = 0;
    for (const auto& bundle : bundles) {
        BundlePeaks peaks;
        juce::String error;
        if (!renderBundle(scratch, bundle, peaks, error)) {
            std::printf("%s: FAILED, %s\n", bundle.getFileName().toRawUTF8(), error.toRawUTF8());
            ++failed;
        } else {
            std::printf("%s\n", lineFor(peaks).toRawUTF8());
            clipping += peaks.clips() ? 1 : 0;
            all.push_back(peaks);
        }
        std::fflush(stdout);
    }
    std::printf("%d bundles, %d clip\n", (int)bundles.size(), clipping);
    if (jsonOut != juce::File())
        writeJson(jsonOut, all);
    return failed > 0 ? 1 : 0;
}

} // namespace render_peaks
