#pragma once

#include "../Timeline/AudioClipStreamer.h"
#include "../Timeline/TimelineSnapshot.h"
#include "../Transport/TransportService.h"
#include "ModuleBase.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <juce_audio_basics/juce_audio_basics.h>

/**
 * @brief "Track Audio" — the graph-side end of a timeline AUDIO track (TL6-4).
 *
 * One node per audio track, created by the add-track flow and bound to the track by the node's
 * uuid, exactly like "Track In" is for a MIDI track. Downstream of it the patch is an ordinary
 * patch: a stereo signal arrives from somewhere and neither knows nor cares that a timeline exists.
 *
 * -- Pull, not push -----------------------------------------------------------------------------
 *
 * Nothing schedules audio into this module. Every block it reads the transport's BlockTimeInfo, the
 * block's TimelineSnapshot and the engine's synth::AudioClipStreamer — all three off the playhead
 * the AudioEngine installs on every node — finds the Audio-kind track whose bindingUuid equals its
 * own node uuid, and renders whichever of that track's clips overlap this block's beat range. A
 * locate, a tempo change or a clip edit therefore takes effect on the very next block with no
 * invalidation step: the module holds no per-clip state at all.
 *
 * The module is INTERNAL-ONLY, the same way "Track In" and "Rec Tap" are: not in the module library,
 * not offered by the replace menu, and explicitly non-authorable by the AI
 * (`kNonAuthorableModuleTypes`, and `validatePatch`'s untrusted path rejects it) — a model that
 * could author one could point playback at a clip, and therefore a file, it chose.
 *
 * -- Streaming, not loading ---------------------------------------------------------------------
 *
 * **THE RAM CONTRACT.** This module and the streamer behind it never hold more than the rings. A
 * ten-minute take costs ONE `AudioClipStreamer::kRingFrames` ring — 1 MiB — not 100 MB, however
 * long the file is and however long the clip sits on the timeline. That is the whole reason this is
 * a separate module from `SamplerModule`, whose whole-file-in-RAM model is right for a one-shot a
 * voice retriggers and wrong for an arrangement. Nothing here ever calls a file API: `readFrames`
 * copies out of a ring the prefetch thread filled, or hands back silence.
 *
 * A clip that has no stream yet (just added, just seeked to, or past
 * `AudioClipStreamer::kMaxStreams`) renders SILENCE — never stale audio, never a stall.
 *
 * -- Sample rate: the v1 policy, stated loudly --------------------------------------------------
 *
 * Source positions are computed in FILE FRAMES using the ENGINE's sample rate, and no resampling
 * happens anywhere. **A file whose header rate differs from the session's plays transposed** (a
 * 44.1 kHz file in a 48 kHz session is ~8.8% sharp and correspondingly short). Record-taps are
 * written at the session rate, so takes recorded in the app are always correct; an imported file
 * may not be. The alternative for v1 — nearest-frame mapping at fill time — would alias audibly on
 * anything with high-frequency content, which is worse than a documented, uniform pitch offset.
 * TODO(resample): a real SRC belongs in the prefetch thread's fill step, where it costs the audio
 * thread nothing. `AudioClipStreamer::getStreamFileSampleRate()` is what a UI warning would read.
 *
 * -- Loop wrap ----------------------------------------------------------------------------------
 *
 * A block that crosses the loop end is TWO beat ranges, not one — the same decomposition Track In
 * makes, from the same BlockTimeInfo fields, with the same multi-wrap bound (only the FIRST wrap in
 * a block is reported, so a loop shorter than one block drops the whole passes in between). Each
 * range is rendered independently and the results SUM, which is what makes a clip straddling the
 * loop boundary end one pass and begin the next inside a single block.
 *
 * -- Hard cuts ----------------------------------------------------------------------------------
 *
 * A stop, a locate or a loop wrap CUTS: the module renders exactly what the new position says and
 * nothing else, so a clip cut off mid-waveform clicks. That is deliberate for TL6-4 — declick ramps
 * (and the crossfade at a loop boundary) are TL6-8's polish, and putting a hidden fade in here now
 * would make the sample-exactness the tests pin unassertable. The one thing that is NEVER heard is
 * garbage: a position the ring cannot serve is silence, never a mix of two places in the file.
 *
 * -- Summing and fades --------------------------------------------------------------------------
 *
 * Overlapping clips SUM (they are legal in the model, and crossfading them is an editor decision,
 * not a playback one). Each clip is multiplied by its own `gainLinear` (converted from dB once, at
 * flatten time) and by a per-sample LINEAR fade envelope derived from `fadeInBeats` / `fadeOutBeats`
 * measured from the clip's own edges. Fades are not clamped against the clip length — a fade longer
 * than the clip simply never reaches unity, which is what the document says and what a later resize
 * must not silently rewrite.
 */
class TimelineAudioSourceModule : public ModuleBase {
public:
    /** Channels the node carries. Fixed for its lifetime (JUCE settles the bus layout in the
     *  ModuleBase constructor, and renegotiating it would drop every connection the node has). */
    static constexpr int kNumChannels = 2;

    /** Frames of per-clip mixing scratch. Bounds the stack/heap footprint of one block: a block
     *  longer than this is rendered in several passes rather than allocating. */
    static constexpr int kScratchFrames = 2048;

    TimelineAudioSourceModule()
        : ModuleBase("Track Audio", 0, kNumChannels) {
        scratch_.setSize(kNumChannels, kScratchFrames);
        scratch_.clear();
        enableVisualBuffer(true);
    }

    ~TimelineAudioSourceModule() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override {
        juce::ignoreUnused(sampleRate, samplesPerBlock);
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override {
        midiMessages.clear(); // a pure audio source: whatever the graph handed us is not ours to pass on

        const int numSamples = buffer.getNumSamples();

        // Bypass: a pure source with NO dry audio path, so both branches clear (the documented
        // exception in the bypass/mute contract — see docs/architecture.md). The buffer the graph
        // hands a source node is scratch whose contents are undefined, so clearing is not optional
        // on any path out of here.
        buffer.clear();
        if (isBypassed()) {
            pushActivity(numSamples, 0.0f);
            return;
        }

        auto* transport = dynamic_cast<synth::TransportService*>(getPlayHead());
        if (transport == nullptr || numSamples <= 0) {
            pushActivity(numSamples, 0.0f);
            return;
        }

        const synth::BlockTimeInfo& info = transport->getCurrentBlockInfo();
        // Both borrowed for THIS BLOCK ONLY — never cached in a member (the exchange frees a
        // superseded snapshot two callbacks after retirement).
        const synth::TimelineSnapshot* snapshot = transport->getCurrentTimelineSnapshot();
        synth::AudioClipStreamer* streamer = transport->getAudioClipStreamer();

        if (!info.playing || snapshot == nullptr || streamer == nullptr) {
            pushActivity(numSamples, 0.0f);
            return;
        }

        const synth::TimelineSnapshot::TrackInfo* track = findMyTrack(*snapshot);
        if (track == nullptr) {
            pushActivity(numSamples, 0.0f);
            return;
        }

        if (track->muted || (snapshot->anySoloed && !track->soloed)) {
            pushActivity(numSamples, 0.0f);
            return;
        }

        renderBlock(*snapshot, *track, info, *streamer, buffer);

        float peak = 0.0f;
        for (int channel = 0; channel < juce::jmin(kNumChannels, buffer.getNumChannels()); ++channel)
            peak = juce::jmax(peak, buffer.getMagnitude(channel, 0, numSamples));
        pushActivity(numSamples, peak);
    }

    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }

    ModuleType getModuleType() const override { return ModuleType::TimelineAudioSource; }
    ModulationCategory getModulationCategory() const override { return ModulationCategory::Other; }

    juce::String getOutputPortLabel(int channelIndex) const override { return channelIndex == 0 ? "Left" : "Right"; }

    LogicalPort mapOutputChannel(int rawChannel) const override {
        LogicalPort port;
        port.visibleJackIndex = juce::jlimit(0, kNumChannels - 1, rawChannel);
        port.role = PortRole::Audio;
        port.isPolyGroupHead = true;
        port.polyVoiceSpan = 1;
        return port;
    }

private:
    // The snapshot track bound to THIS node: an AUDIO track whose bindingUuid equals our node uuid.
    // An empty node uuid matches nothing — an unsaved node has no identity yet, and "" would
    // otherwise match every unbound track in the document.
    const synth::TimelineSnapshot::TrackInfo* findMyTrack(const synth::TimelineSnapshot& snapshot) const noexcept {
        const char* myUuid = getNodeUuid();
        if (myUuid[0] == '\0')
            return nullptr;

        for (const auto& track : snapshot.tracks)
            if (track.kind == static_cast<int>(synth::TrackKind::Audio) && std::strcmp(track.bindingUuid, myUuid) == 0)
                return &track;

        return nullptr;
    }

    // One block = one beat range, or two when it wraps the loop. Mirrors
    // TimelineMidiSourceModule::emitBlock exactly — same fields, same clamping, same multi-wrap
    // bound — because the two modules must agree on which beats belong to this block or a MIDI
    // track and an audio track would drift apart across a loop.
    void renderBlock(const synth::TimelineSnapshot& snapshot, const synth::TimelineSnapshot::TrackInfo& track,
                     const synth::BlockTimeInfo& info, synth::AudioClipStreamer& streamer,
                     juce::AudioBuffer<float>& buffer) {
        const double beatsPerSample = info.beatsPerSample();
        if (!(beatsPerSample > 0.0) || !(info.sampleRate > 0.0))
            return;

        const bool wraps = info.loopWrapSample >= 0;
        const int lastSample = juce::jmax(0, info.numSamples - 1);

        // PRIMARY range, capped at the loop end when this block wraps: info.endPpq is the UNWRAPPED
        // virtual end and overshoots loopEndPpq, so using it here would render beats the transport
        // never plays.
        renderRange(snapshot, track, info, streamer, buffer, info.startPpq, wraps ? info.loopEndPpq : info.endPpq,
                    /*baseOffset=*/0, beatsPerSample);

        if (!wraps)
            return;

        const int wrapOffset = juce::jlimit(0, lastSample, info.loopWrapSample);
        // WRAPPED range: from the loop start to where the transport ACTUALLY lands after this block,
        // so the two ranges tile the timeline exactly with no beat rendered twice and none skipped.
        renderRange(snapshot, track, info, streamer, buffer, info.loopStartPpq,
                    beatFromSample(predictNextBlockStart(info), info), wrapOffset, beatsPerSample);
    }

    void renderRange(const synth::TimelineSnapshot& snapshot, const synth::TimelineSnapshot::TrackInfo& track,
                     const synth::BlockTimeInfo& info, synth::AudioClipStreamer& streamer,
                     juce::AudioBuffer<float>& buffer, double rangeStart, double rangeEnd, int baseOffset,
                     double beatsPerSample) {
        if (!(rangeEnd > rangeStart))
            return;

        const int first = track.firstAudioClip;
        const int last = track.firstAudioClip + track.numAudioClips;

        for (int i = first; i < last && i < (int)snapshot.audioClips.size(); ++i) {
            const auto& clip = snapshot.audioClips[(std::size_t)i];
            if (clip.assetRef[0] == '\0' || !(clip.lengthBeats > 0.0))
                continue;
            if (clip.startBeat >= rangeEnd)
                break; // the run is sorted by startBeat: everything after this one is later still

            renderClip(clip, info, streamer, buffer, rangeStart, rangeEnd, baseOffset, beatsPerSample);
        }
    }

    void renderClip(const synth::TimelineSnapshot::AudioClipInfo& clip, const synth::BlockTimeInfo& info,
                    synth::AudioClipStreamer& streamer, juce::AudioBuffer<float>& buffer, double rangeStart,
                    double rangeEnd, int baseOffset, double beatsPerSample) {
        const double clipEnd = clip.startBeat + clip.lengthBeats;
        const double overlapStart = juce::jmax(rangeStart, clip.startBeat);
        const double overlapEnd = juce::jmin(rangeEnd, clipEnd);
        if (!(overlapEnd > overlapStart))
            return;

        const int blockSamples = buffer.getNumSamples();
        const int startOffset = beatToOffset(overlapStart, rangeStart, beatsPerSample, baseOffset, blockSamples);
        const int endOffset = beatToOffset(overlapEnd, rangeStart, beatsPerSample, baseOffset, blockSamples);
        const int frames = endOffset - startOffset;
        if (frames <= 0)
            return;

        // Beat -> FILE FRAME. The engine's sample rate is used deliberately as if it were the file's
        // — see the class comment's sample-rate policy. The clip's own trim (`sourceStartSeconds`)
        // is measured in seconds because it indexes a recording, whose samples do not move when the
        // tempo map does.
        const double secondsPerBeat = (info.bpm > 0.0) ? (60.0 / info.bpm) : 0.5;
        const std::int64_t sourceFrame = (std::int64_t)std::llround(
            ((overlapStart - clip.startBeat) * secondsPerBeat + clip.sourceStartSeconds) * info.sampleRate);

        const auto* handle = streamer.acquire(clip.clipId);

        if (buffer.getNumChannels() <= 0)
            return;
        float* destLeft = buffer.getWritePointer(0);
        // A one-channel buffer (a host or test that prepared the graph mono) gets the left channel
        // only rather than a fold-down: the node declares two outputs, and inventing a mix here
        // would make what "channel 0" carries depend on the render format.
        float* destRight = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : nullptr;

        // Rendered in scratch-sized chunks so a block longer than kScratchFrames costs another pass
        // rather than an allocation on the audio thread.
        for (int done = 0; done < frames; done += kScratchFrames) {
            const int chunk = juce::jmin(kScratchFrames, frames - done);
            float* left = scratch_.getWritePointer(0);
            float* right = scratch_.getWritePointer(1);

            if (handle != nullptr) {
                streamer.readFrames(handle, sourceFrame + done, left, right, chunk);
            } else {
                // No stream: the clip is beyond the pool cap, unresolvable, or not open yet.
                std::memset(left, 0, sizeof(float) * (std::size_t)chunk);
                std::memset(right, 0, sizeof(float) * (std::size_t)chunk);
            }

            // Gain x fade, per sample, then SUM into the block. The beat is stepped rather than
            // recomputed from the offset so the envelope is exactly linear in beats.
            double beat = overlapStart + (double)done * beatsPerSample;
            for (int i = 0; i < chunk; ++i, beat += beatsPerSample) {
                const float envelope = clip.gainLinear * fadeGainAt(clip, clipEnd, beat);
                const int index = startOffset + done + i;
                destLeft[index] += left[i] * envelope;
                if (destRight != nullptr)
                    destRight[index] += right[i] * envelope;
            }
        }
    }

    // Linear fades measured from the clip's own edges, in beats. Deliberately NOT clamped against
    // the clip length: a fade longer than the clip never reaches unity, which is exactly what
    // TimelineDoc::setClipFades stores and what a later resize must not silently rewrite.
    static float fadeGainAt(const synth::TimelineSnapshot::AudioClipInfo& clip, double clipEnd, double beat) noexcept {
        float gain = 1.0f;
        if (clip.fadeInBeats > 0.0)
            gain *= (float)juce::jlimit(0.0, 1.0, (beat - clip.startBeat) / clip.fadeInBeats);
        if (clip.fadeOutBeats > 0.0)
            gain *= (float)juce::jlimit(0.0, 1.0, (clipEnd - beat) / clip.fadeOutBeats);
        return gain;
    }

    // Beat -> block-relative sample offset, clamped into [baseOffset, blockSamples]. The
    // intermediate clamp on the beat distance keeps llround away from an out-of-range double (a
    // locate plus a tempo change can make the raw difference astronomically large). Unlike Track
    // In's version the upper bound is blockSamples, not blockSamples - 1: this returns a RANGE end,
    // and a clip filling the block must be allowed to reach one past the last sample.
    static int beatToOffset(double beat, double rangeStart, double beatsPerSample, int baseOffset,
                            int blockSamples) noexcept {
        const double rel = juce::jlimit(-1.0e9, 1.0e9, (beat - rangeStart) / beatsPerSample);
        const std::int64_t offset = (std::int64_t)baseOffset + std::llround(rel);
        return (int)juce::jlimit<std::int64_t>(baseOffset, juce::jmax(baseOffset, blockSamples), offset);
    }

    static double beatFromSample(std::int64_t samplePosition, const synth::BlockTimeInfo& info) noexcept {
        return (info.sampleRate > 0.0) ? (double)samplePosition * info.bpm / (60.0 * info.sampleRate) : 0.0;
    }

    static std::int64_t sampleFromBeat(double beat, const synth::BlockTimeInfo& info) noexcept {
        return (info.bpm > 0.0) ? (std::int64_t)std::llround(beat * 60.0 * info.sampleRate / info.bpm) : 0;
    }

    // Where the NEXT block will start, mirroring TransportService::tick()'s loop fold exactly — the
    // same function TimelineMidiSourceModule uses, for the same reason: the wrapped range's end is
    // precisely this position expressed in beats, which is what makes the two ranges tile.
    static std::int64_t predictNextBlockStart(const synth::BlockTimeInfo& info) noexcept {
        if (!info.playing || info.numSamples <= 0)
            return info.blockStartSample;

        std::int64_t endSample = info.blockStartSample + info.numSamples;
        if (info.looping && info.loopEndPpq > info.loopStartPpq) {
            const std::int64_t loopStartSample = sampleFromBeat(info.loopStartPpq, info);
            const std::int64_t loopEndSample = sampleFromBeat(info.loopEndPpq, info);
            const std::int64_t loopLength = juce::jmax<std::int64_t>(1, loopEndSample - loopStartSample);
            if (info.blockStartSample < loopEndSample && endSample >= loopEndSample)
                endSample = loopStartSample + (endSample - loopEndSample) % loopLength;
        }
        return endSample;
    }

    void pushActivity(int numSamples, float level) {
        if (auto* vb = getVisualBuffer())
            for (int i = 0; i < numSamples; ++i)
                vb->pushSample(level);
    }

    // Per-clip mixing scratch, sized once in the constructor. processBlock never resizes it.
    juce::AudioBuffer<float> scratch_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TimelineAudioSourceModule)
};
