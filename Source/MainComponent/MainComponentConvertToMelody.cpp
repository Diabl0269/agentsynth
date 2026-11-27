// MainComponentConvertToMelody.cpp -- "Convert to melody" (docs/timeline/clips.md#convert-to-melody). The audio range
// is read and analysed on a background thread with its own reader; the result comes back to the message thread, which
// re-checks the clip and adds the track, clip and notes in one undo step.
#include "MainComponentConvertToMelody.h"

#include "AI/AIStateMapper/AIStateMapper.h"
#include "AudioEngine/AudioEngine.h"
#include "MainComponent.h"
#include "Timeline/Melody/HumToMelody.h"
#include "Transport/TransportNudge.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include <algorithm>
#include <juce_audio_formats/juce_audio_formats.h>

namespace {

constexpr double kFallbackBpm = 120.0;
constexpr double kMaxAnalysedSeconds = 600.0;
constexpr double kDefaultGridBeats = 0.25;
constexpr const char* kDefaultInstrument = "Oscillator"; // what Add track > Instrument > Oscillator builds

// Everything the background thread needs, copied out of the document so it never touches it.
struct Job {
    juce::File file;
    double startSeconds = 0.0;
    double lengthSeconds = 0.0;
    synth::melody::MelodyOptions options;
    synth::ClipId clip;
    bool songHasKey = false;
};

// The range as mono floats (channels averaged); empty when the file cannot be read. Reads in chunks so a long file
// never needs a second full-size buffer.
std::vector<float> readMono(const Job& job, double& sampleRate) {
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    const std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(job.file));
    if (reader == nullptr || reader->numChannels == 0 || reader->sampleRate <= 0.0)
        return {};
    sampleRate = reader->sampleRate;
    const auto first = (juce::int64)(job.startSeconds * sampleRate);
    const auto wanted = (juce::int64)(std::min(job.lengthSeconds, kMaxAnalysedSeconds) * sampleRate);
    const auto available = std::min(wanted, reader->lengthInSamples - first);
    if (available <= 0)
        return {};

    std::vector<float> mono;
    mono.reserve((size_t)available);
    constexpr int kChunk = 65536;
    const int channels = (int)reader->numChannels;
    juce::AudioBuffer<float> chunk(channels, kChunk);
    for (juce::int64 done = 0; done < available; done += kChunk) {
        const int frames = (int)std::min<juce::int64>(kChunk, available - done);
        if (!reader->read(&chunk, 0, frames, first + done, true, true))
            return {};
        for (int i = 0; i < frames; ++i) {
            float sum = 0.0f;
            for (int c = 0; c < channels; ++c)
                sum += chunk.getSample(c, i);
            mono.push_back(sum / (float)channels);
        }
    }
    return mono;
}

} // namespace

void MainComponentConvertToMelody::request(MainComponent& mc, synth::ClipId id, bool snapToKey) {
    const auto* clip = mc.timelineDoc.getClip(id);
    if (clip == nullptr || clip->assetRef.isEmpty())
        return;
    Job job;
    job.file = mc.audioEngine.getAudioClipStreamer().resolveAssetRef(clip->assetRef);
    if (!job.file.existsAsFile()) {
        mc.statusBar.showMessage("The clip's audio file is missing");
        return;
    }
    const auto snapshot = mc.audioEngine.getTransport().getPositionSnapshot();
    const double bpm = snapshot.bpm > 0.0 ? snapshot.bpm : kFallbackBpm;
    const double secondsPerBeat = synth::clipSecondsPerBeat(*clip, 60.0 / bpm);
    const auto songKey = mc.audioEngine.getTransport().getDocumentState().songKey;

    job.startSeconds = clip->sourceStartSeconds;
    job.lengthSeconds = clip->lengthBeats * secondsPerBeat;
    job.clip = id;
    job.songHasKey = songKey.has_value();
    job.options.mode = snapToKey ? synth::melody::MelodyMode::SnapToKey : synth::melody::MelodyMode::AsSung;
    job.options.key = songKey;
    job.options.secondsPerBeat = secondsPerBeat;
    job.options.lengthBeats = std::min(clip->lengthBeats, kMaxAnalysedSeconds / secondsPerBeat);
    const double grid = mc.timelinePanel.getViewState().divisionBeatsRaw(synth::beatsPerBarOf(snapshot));
    job.options.gridBeats = grid > 0.0 ? grid : kDefaultGridBeats;

    mc.statusBar.showMessage("Finding the melody in " + clip->name + "...");
    juce::Component::SafePointer<MainComponent> safeThis(&mc);
    juce::Thread::launch([safeThis, job] {
        double sampleRate = 0.0;
        const auto mono = readMono(job, sampleRate);
        std::vector<synth::melody::MelodyNote> notes;
        synth::MusicalScale usedKey;
        if (!mono.empty())
            notes = synth::melody::humToMelody(mono.data(), (int)mono.size(), sampleRate, job.options, &usedKey);
        const std::optional<synth::MusicalScale> clipKey =
            (!job.songHasKey && job.options.mode == synth::melody::MelodyMode::SnapToKey)
                ? std::optional<synth::MusicalScale>(usedKey)
                : std::nullopt;
        juce::MessageManager::callAsync([safeThis, id = job.clip, notes = std::move(notes), clipKey] {
            if (auto* self = safeThis.getComponent())
                MainComponentConvertToMelody::apply(*self, id, notes, clipKey);
        });
    });
}

// The track is the plain "Add track > Instrument > Oscillator" build (its channel chain, macro and Master wiring),
// minus the user's default-track-preset lookup: a preset could be any chain, and the melody's sound should not depend
// on it. The clip lies exactly over the audio clip, so the melody plays where the take was.
synth::ClipId MainComponentConvertToMelody::apply(MainComponent& mc, synth::ClipId audioClip,
                                                  const std::vector<synth::melody::MelodyNote>& notes,
                                                  const std::optional<synth::MusicalScale>& clipKey) {
    const auto* source = mc.timelineDoc.getClip(audioClip);
    if (source == nullptr)
        return {};
    if (notes.empty()) {
        mc.statusBar.showMessage("No melody found in this clip");
        return {};
    }
    const juce::String name = source->name + " melody";
    const double startBeat = source->startBeat;
    const double lengthBeats = source->lengthBeats;

    const MainComponent::TrackGrowInScope growIn(mc);
    auto staged =
        std::make_shared<std::unique_ptr<juce::AudioProcessor>>(synth::AIStateMapper::createModule(kDefaultInstrument));
    if (*staged == nullptr) {
        mc.statusBar.showMessage("Could not add a track");
        return {};
    }
    synth::ClipId newClip;
    const bool pushed = mc.undoManager.recordGraphTimelineAndMacroChange(
        mc.audioEngine.getGraph(), mc.timelineDoc, mc.graphEditor.getMacros(), [&] {
            if (!mc.buildInstrumentTrackBody(staged, name, /*poly=*/false, {}, {}))
                return;
            const auto track = mc.timelineDoc.getTracks().back().id;
            newClip = mc.timelineDoc.addClip(track, startBeat, lengthBeats, name);
            if (!newClip.isValid())
                return;
            int added = 0;
            for (const auto& note : notes) {
                if (added >= synth::TimelineDoc::kMaxNotesPerClip)
                    break;
                synth::MidiNote midi;
                midi.startBeat = note.startBeat;
                midi.lengthBeats = note.lengthBeats;
                midi.pitch = note.pitch;
                midi.velocity = std::clamp(note.velocity, 1, 127);
                midi.channel = 1;
                if (mc.timelineDoc.addNote(newClip, midi).isValid())
                    ++added;
            }
            if (clipKey.has_value())
                mc.timelineDoc.setClipScale(newClip, clipKey, false);
        });
    mc.audioEngine.updateModuleNames();
    mc.reconcileTimelineAfterGraphChange();
    if (!pushed || !newClip.isValid()) {
        mc.statusBar.showMessage("Could not add a track");
        return {};
    }
    mc.timelinePanel.getClipSelection().setSelection({newClip});
    mc.timelinePanel.getClipLaneArea().repaint();
    mc.statusBar.showMessage("Added " + name);
    return newClip;
}
