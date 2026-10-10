#pragma once

// synth::TimelineOpsHost for MainComponent: the graph-side half of a timelineOps batch, kept in its
// own adapter rather than added to MainComponent's base list. See MainComponentTimelineOpsHost.cpp.

#include "Timeline/TimelineOps.h"

class MainComponent;

class MainComponentTimelineOpsHost final : public synth::TimelineOpsHost {
public:
    /** `owner` must outlive this object (it is a MainComponent member). Message thread only. */
    explicit MainComponentTimelineOpsHost(MainComponent& owner) noexcept
        : owner_(owner) {}

    std::optional<synth::InstrumentTrackBuildResult>
    addInstrumentTrack(const juce::String& name, const juce::String& instrumentType, bool poly,
                       const std::vector<synth::InstrumentTrackInsert>& inserts, const juce::var& envelopeParams,
                       const juce::var& instrumentParams) override;
    bool recordBatch(const std::function<void()>& mutation) override;
    synth::TimelineDoc* editableTimelineDoc() override;
    void placeNewModules(const std::vector<juce::AudioProcessorGraph::NodeID>& created) override;
    bool canSetTempo() const override { return true; }
    bool setTempo(double bpm) override;

private:
    MainComponent& owner_;
    bool tempoRecorded_ = false; // a setTempo in the running batch pushed an undo step
};
