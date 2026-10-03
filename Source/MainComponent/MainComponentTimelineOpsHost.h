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

    bool addInstrumentTrack(const juce::String& name, const juce::String& instrumentType, bool poly,
                            const std::vector<synth::InstrumentTrackInsert>& inserts,
                            juce::String& instrumentUuid) override;
    bool recordBatch(const std::function<void()>& mutation) override;

private:
    MainComponent& owner_;
};
