#pragma once

#include <functional>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>
#include <set>

namespace synth {

/** Lets a scale audition be heard through a muted sound or while another sound is soloed. While a run is active, the
 *  node it plays through and every node downstream of it (instrument, inserts, channel strip, send buses, Master) are
 *  flagged "audible": a flagged module renders as if its Mute were off, and a flagged strip or Master ignores the mixer
 *  solo gate. Nothing else is touched, and no parameter is written, so the mixer keeps showing its real mute and solo.
 *  The flags stay on for `kTailMs` after the last note so a release tail is not cut, and drop at once when the
 *  transport starts playing, so a muted sound never leaks into the song. Message thread only. */
class AuditionMonitor : private juce::Timer {
public:
    using NodeID = juce::AudioProcessorGraph::NodeID;

    /** `playbackRunning` says whether the transport is playing; the flags are dropped as soon as it is. */
    AuditionMonitor(juce::AudioProcessorGraph& graph, std::function<bool()> playbackRunning);
    ~AuditionMonitor() override;

    /** Flags `source` and everything downstream of it; replaces an earlier run's flags. Cancels a pending tail. */
    void begin(NodeID source);
    /** The run's last note-off has been sent: the flags clear after `kTailMs`. */
    void end();
    /** Clears the flags now. */
    void clear();
    bool isActive() const noexcept { return !flagged_.empty(); }
    const std::set<NodeID>& flaggedNodes() const noexcept { return flagged_; }

    /** Test seam: runs the check the timer does every kPollMs. */
    void pollForTest() { timerCallback(); }

    static constexpr int kTailMs = 2500;
    static constexpr int kPollMs = 40;

private:
    void timerCallback() override;

    juce::AudioProcessorGraph& graph_;
    std::function<bool()> playbackRunning_;
    std::set<NodeID> flagged_;
    bool ended_ = false;
    juce::uint32 clearAtMs_ = 0;

    JUCE_DECLARE_NON_COPYABLE(AuditionMonitor)
};

} // namespace synth
