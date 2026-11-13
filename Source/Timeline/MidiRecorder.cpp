#include "MidiRecorder.h"
#include "../AppUndoManager.h"
#include <algorithm>
#include <cmath>
#include <iterator>
#include <map>
#include <utility>
#include <vector>

namespace synth {

void MidiRecorder::captureBlock(const juce::MidiBuffer& midi, const BlockTimeInfo& info,
                                const SourcedNotes& sourced) noexcept {
    if (!recording.load(std::memory_order_relaxed) || !info.playing)
        return;
    if (!(info.beatsPerSample() > 0.0))
        return;

    // A count-in's pre-roll bars are live through the transport (and this flag) but must
    // never land in the committed take — startRecording() sets this to the punch-in point, and
    // "record while already playing" / "count-in off" both pass the CURRENT position, making the
    // filter a no-op for every take that isn't a count-in pre-roll.
    const double punchInBeat = punchInBeat_.load(std::memory_order_relaxed);

    for (const auto metadata : midi) {
        // Filter on the raw byte count BEFORE touching the data as a message: building a
        // juce::MidiMessage heap-allocates for anything longer than its 8-byte inline buffer (SysEx,
        // MTC full frame), and this runs on the audio thread. Note on/off is always 3 bytes.
        if (metadata.numBytes != 3)
            continue;
        captureNote(kAnySource, metadata.data, metadata.samplePosition, info, punchInBeat);
    }

    // The device-filtered targets read the per-device copy instead. The engine drains it once per device callback
    // and a sliced callback renders several passes, so each pass takes only the events inside its own slice.
    for (int i = 0; i < sourced.count; ++i) {
        const auto& event = sourced.events[i];
        const int position = event.samplePosition - sourced.offset;
        if (position >= 0 && position < sourced.length)
            captureNote(event.source, event.bytes, position, info, punchInBeat);
    }
}

// Every target whose source and channel match gets its own copy of the note, so two targets on the same input (say
// one per channel) each record theirs.
void MidiRecorder::captureNote(int source, const std::uint8_t* bytes, int samplePosition, const BlockTimeInfo& info,
                               double punchInBeat) noexcept {
    const int status = bytes[0] & 0xf0;
    const bool isOn = status == 0x90 && bytes[2] > 0;
    const bool isOff = status == 0x80 || (status == 0x90 && bytes[2] == 0);
    if (!isOn && !isOff)
        return;

    const double beatsPerSample = info.beatsPerSample();
    double beat;
    if (info.loopWrapSample >= 0 && samplePosition >= info.loopWrapSample)
        beat = info.loopStartPpq + (double)(samplePosition - info.loopWrapSample) * beatsPerSample;
    else
        beat = info.startPpq + (double)samplePosition * beatsPerSample;
    if (beat < punchInBeat)
        return; // pre-roll: heard, but never recorded

    Event event;
    event.beat = beat;
    event.pitch = bytes[1] & 0x7f;
    event.velocity = isOn ? (bytes[2] & 0x7f) : 0;
    event.channel = (bytes[0] & 0x0f) + 1;
    event.isNoteOn = isOn;

    const int count = targetCount_.load(std::memory_order_relaxed);
    for (int t = 0; t < count; ++t) {
        if (targetSources_[(size_t)t].load(std::memory_order_relaxed) != source)
            continue;
        const int channel = targetChannels_[(size_t)t].load(std::memory_order_relaxed);
        if (channel != 0 && channel != event.channel)
            continue;
        event.target = t;
        pushEvent(event);
    }
}

void MidiRecorder::pushEvent(const Event& event) noexcept {
    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    ring.prepareToWrite(1, start1, size1, start2, size2);
    if (size1 + size2 < 1) {
        // Full: drop rather than block the audio thread. Surfaced later via hadOverrun().
        overrunFlag.store(true, std::memory_order_relaxed);
        return;
    }
    ringSlots[(size_t)start1] = event;
    ring.finishedWrite(1);
}

void MidiRecorder::startRecording(TrackId armedTrackIn, double punchInBeatIn) {
    startRecording(std::vector<TakeTarget>{{armedTrackIn, kAnySource, 0}}, punchInBeatIn);
}

void MidiRecorder::startRecording(const std::vector<TakeTarget>& targets, double punchInBeatIn) {
    // Disarm first: guarantees no further audio-thread writes land in the ring while it's being
    // reset (captureBlock checks `recording` before touching the ring at all).
    recording.store(false, std::memory_order_relaxed);
    ring.reset();
    overrunFlag.store(false, std::memory_order_relaxed);
    const int count = std::min((int)targets.size(), kMaxTargets);
    for (int t = 0; t < count; ++t) {
        const auto& target = targets[(size_t)t];
        targetTracks_[(size_t)t] = target.track;
        targetSources_[(size_t)t].store(target.source, std::memory_order_relaxed);
        targetChannels_[(size_t)t].store(target.channel, std::memory_order_relaxed);
    }
    targetCount_.store(count, std::memory_order_relaxed);
    punchInBeat_.store(punchInBeatIn, std::memory_order_relaxed);
    recording.store(true, std::memory_order_relaxed);
}

namespace {

struct RecordedNote {
    double startBeat;
    double lengthBeats;
    int pitch;
    int velocity;
    int channel;
};

// One take's clip: its notes and the whole beats they span.
struct RecordedClip {
    TrackId track;
    double start = 0.0;
    double length = 0.0;
    std::vector<RecordedNote> notes;
};

} // namespace

// FIFO per (pitch, channel): an off closes the earliest still-open on for that key, so overlapping same-pitch
// retriggers pair up in the order they were struck. Anything still open never got a note-off and is closed at
// `stopBeat`; a stray off with no on is ignored.
template <typename EventT>
static std::vector<RecordedNote> pairNotes(const std::vector<EventT>& events, double stopBeat, double minLength) {
    std::vector<RecordedNote> notes;
    notes.reserve(events.size() / 2 + 1);
    std::map<std::pair<int, int>, std::vector<EventT>> open;
    for (const auto& e : events) {
        const auto key = std::make_pair(e.pitch, e.channel);
        if (e.isNoteOn) {
            open[key].push_back(e);
            continue;
        }
        auto it = open.find(key);
        if (it == open.end() || it->second.empty())
            continue;
        const EventT on = it->second.front();
        it->second.erase(it->second.begin());
        notes.push_back({on.beat, e.beat - on.beat, on.pitch, on.velocity, on.channel});
    }
    for (auto& [key, opens] : open) {
        juce::ignoreUnused(key);
        for (const auto& on : opens)
            notes.push_back({on.beat, std::max(stopBeat - on.beat, minLength), on.pitch, on.velocity, on.channel});
    }
    return notes;
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
    // timing state with the audio thread beyond the ring, so the latest beat actually captured (by
    // any target) is used to close out a still-held note.
    double stopBeat = events.front().beat;
    for (const auto& e : events)
        stopBeat = std::max(stopBeat, e.beat);

    std::vector<RecordedClip> clips;
    const int targetCount = targetCount_.load(std::memory_order_relaxed);
    for (int t = 0; t < targetCount; ++t) {
        std::vector<Event> own;
        std::copy_if(events.begin(), events.end(), std::back_inserter(own),
                     [t](const Event& e) { return e.target == t; });
        auto notes = pairNotes(own, stopBeat, kMinNoteLengthBeats);
        if (notes.empty())
            continue; // nothing captured for this target, or only stray note-offs

        double clipStart = notes.front().startBeat;
        double clipEnd = notes.front().startBeat + notes.front().lengthBeats;
        for (const auto& n : notes) {
            clipStart = std::min(clipStart, n.startBeat);
            clipEnd = std::max(clipEnd, n.startBeat + n.lengthBeats);
        }
        clipStart = std::floor(clipStart);
        clipEnd = std::ceil(clipEnd);
        clips.push_back({targetTracks_[(size_t)t], clipStart, std::max(clipEnd - clipStart, kMinNoteLengthBeats),
                         std::move(notes)});
    }
    if (clips.empty())
        return false;

    // Every target's clip in ONE undo step: one Record press is one thing to undo.
    return undo.recordTimelineChange(doc, [&] {
        for (const auto& recorded : clips) {
            const ClipId clip = doc.addClip(recorded.track, recorded.start, recorded.length, "Take");
            if (!clip.isValid())
                continue; // invalid track or the track is already at kMaxClipsPerTrack: nothing for it
            for (const auto& n : recorded.notes) {
                MidiNote note;
                note.startBeat = n.startBeat - recorded.start;
                note.lengthBeats = n.lengthBeats;
                note.pitch = n.pitch;
                note.velocity = n.velocity;
                note.channel = n.channel;
                doc.addNote(clip, note);
            }
        }
    });
}

} // namespace synth
