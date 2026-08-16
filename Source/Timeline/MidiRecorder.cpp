#include "MidiRecorder.h"
#include "../AppUndoManager.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <utility>
#include <vector>

namespace synth {

void MidiRecorder::captureBlock(const juce::MidiBuffer& midi, const BlockTimeInfo& info) noexcept {
    if (!recording.load(std::memory_order_relaxed) || !info.playing)
        return;

    const double beatsPerSample = info.beatsPerSample();
    if (!(beatsPerSample > 0.0))
        return;

    // A count-in's pre-roll bars are live through the transport (and this flag) but must
    // never land in the committed take — startRecording() sets this to the punch-in point, and
    // "record while already playing" / "count-in off" both pass the CURRENT position, making the
    // filter a no-op for every take that isn't a count-in pre-roll.
    const double punchInBeat = punchInBeat_.load(std::memory_order_relaxed);

    for (const auto metadata : midi) {
        const auto message = metadata.getMessage();
        const bool isOn = message.isNoteOn();
        const bool isOff = message.isNoteOff();
        if (!isOn && !isOff)
            continue;

        const int samplePosition = metadata.samplePosition;
        double beat;
        if (info.loopWrapSample >= 0 && samplePosition >= info.loopWrapSample)
            beat = info.loopStartPpq + (double)(samplePosition - info.loopWrapSample) * beatsPerSample;
        else
            beat = info.startPpq + (double)samplePosition * beatsPerSample;

        if (beat < punchInBeat)
            continue; // pre-roll: heard, but never recorded

        Event event;
        event.beat = beat;
        event.pitch = message.getNoteNumber();
        event.velocity = (int)message.getVelocity();
        event.channel = message.getChannel();
        event.isNoteOn = isOn;

        int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
        ring.prepareToWrite(1, start1, size1, start2, size2);
        if (size1 + size2 < 1) {
            // Full: drop rather than block the audio thread. Surfaced later via hadOverrun().
            overrunFlag.store(true, std::memory_order_relaxed);
            continue;
        }
        ringSlots[(size_t)start1] = event;
        ring.finishedWrite(1);
    }
}

void MidiRecorder::startRecording(TrackId armedTrackIn, double punchInBeatIn) {
    // Disarm first: guarantees no further audio-thread writes land in the ring while it's being
    // reset (captureBlock checks `recording` before touching the ring at all).
    recording.store(false, std::memory_order_relaxed);
    ring.reset();
    overrunFlag.store(false, std::memory_order_relaxed);
    armedTrack = armedTrackIn;
    punchInBeat_.store(punchInBeatIn, std::memory_order_relaxed);
    recording.store(true, std::memory_order_relaxed);
}

bool MidiRecorder::stopAndCommit(TimelineDoc& doc, AppUndoManager& undo) {
    recording.store(false, std::memory_order_relaxed);

    // Drain the whole ring into a local buffer. Message thread only — allocation here is fine.
    std::vector<Event> events;
    for (;;) {
        const int numReady = ring.getNumReady();
        if (numReady <= 0)
            break;
        int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
        ring.prepareToRead(numReady, start1, size1, start2, size2);
        events.reserve(events.size() + (size_t)(size1 + size2));
        for (int i = 0; i < size1; ++i)
            events.push_back(ringSlots[(size_t)(start1 + i)]);
        for (int i = 0; i < size2; ++i)
            events.push_back(ringSlots[(size_t)(start2 + i)]);
        ring.finishedRead(size1 + size2);
    }

    if (events.empty())
        return false;

    // The best available proxy for "the position recording stopped at": this class shares no
    // timing state with the audio thread beyond the ring, so the latest beat actually captured is
    // used to close out a still-held note.
    double stopBeat = events.front().beat;
    for (const auto& e : events)
        stopBeat = std::max(stopBeat, e.beat);

    struct RecordedNote {
        double startBeat;
        double lengthBeats;
        int pitch;
        int velocity;
        int channel;
    };
    std::vector<RecordedNote> notes;
    notes.reserve(events.size() / 2 + 1);

    // FIFO per (pitch, channel): an off closes the earliest still-open on for that key, so
    // overlapping same-pitch retriggers pair up in the order they were struck.
    std::map<std::pair<int, int>, std::vector<Event>> open;
    for (const auto& e : events) {
        const auto key = std::make_pair(e.pitch, e.channel);
        if (e.isNoteOn) {
            open[key].push_back(e);
        } else {
            auto it = open.find(key);
            if (it == open.end() || it->second.empty())
                continue; // an off without an on is ignored
            const Event on = it->second.front();
            it->second.erase(it->second.begin());
            notes.push_back({on.beat, e.beat - on.beat, on.pitch, on.velocity, on.channel});
        }
    }
    // Anything still open never got a note-off: close it at the stop position.
    for (auto& [key, opens] : open) {
        juce::ignoreUnused(key);
        for (const auto& on : opens) {
            const double length = std::max(stopBeat - on.beat, kMinNoteLengthBeats);
            notes.push_back({on.beat, length, on.pitch, on.velocity, on.channel});
        }
    }

    if (notes.empty())
        return false; // every captured event was a stray note-off

    double clipStart = notes.front().startBeat;
    double clipEnd = notes.front().startBeat + notes.front().lengthBeats;
    for (const auto& n : notes) {
        clipStart = std::min(clipStart, n.startBeat);
        clipEnd = std::max(clipEnd, n.startBeat + n.lengthBeats);
    }
    clipStart = std::floor(clipStart);
    clipEnd = std::ceil(clipEnd);
    const double clipLength = std::max(clipEnd - clipStart, kMinNoteLengthBeats);

    const TrackId track = armedTrack;
    return undo.recordTimelineChange(doc, [&] {
        const ClipId clip = doc.addClip(track, clipStart, clipLength, "Take");
        if (!clip.isValid())
            return; // invalid track or the track is already at kMaxClipsPerTrack: a no-op commit
        for (const auto& n : notes) {
            MidiNote note;
            note.startBeat = n.startBeat - clipStart;
            note.lengthBeats = n.lengthBeats;
            note.pitch = n.pitch;
            note.velocity = n.velocity;
            note.channel = n.channel;
            doc.addNote(clip, note);
        }
    });
}

} // namespace synth
