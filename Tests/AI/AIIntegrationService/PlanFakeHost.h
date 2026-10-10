// PlanFakeHost.h -- a TimelineOpsHost double for edit-plan tests, shared by the service's own
// edit-plan tests and the chat's edit-plan card tests. Header-only; not registered in CMake.
#pragma once

#include "AI/AIIntegrationService/AIIntegrationService.h"
#include "AI/AIStateMapper/AIStateMapper.h"
#include "AppUndoManager.h"
#include "MacroSet.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include <optional>
#include <vector>

namespace synth {

// Builds a track like the app would, minus wiring: the doc track, the instrument, its envelope and
// each insert, each with a uuid. `failOnBuild` makes that build (1-based) fail AFTER leaving a stray node behind,
// breaking the host contract on purpose so the service's backstop is what restores the graph.
class PlanFakeHost : public TimelineOpsHost {
public:
    PlanFakeHost(TimelineDoc& d, juce::AudioProcessorGraph& g)
        : doc(d)
        , graph(g) {}

    std::optional<InstrumentTrackBuildResult> addInstrumentTrack(const juce::String& name, const juce::String& type,
                                                                 bool,
                                                                 const std::vector<InstrumentTrackInsert>& inserts,
                                                                 const juce::var& envelopeParams,
                                                                 const juce::var& instrumentParams) override {
        ++builds;
        if (builds == failOnBuild) {
            graph.addNode(AIStateMapper::createModule("LFO"));
            return std::nullopt;
        }
        if (!doc.addTrack(TrackKind::Midi, name).isValid())
            return std::nullopt;
        InstrumentTrackBuildResult result;
        result.instrumentUuid = add(type, instrumentParams);
        if (type != "Sampler" && !withoutEnvelope)
            result.envelopeUuid = add("ADSR", envelopeParams);
        for (const auto& insert : inserts)
            result.insertUuids.push_back(add(insert.type, insert.params));
        return result;
    }
    bool recordBatch(const std::function<void()>& mutation) override {
        ++batches;
        return undo.recordGraphTimelineAndMacroChange(graph, doc, macros, mutation);
    }
    TimelineDoc* editableTimelineDoc() override { return &doc; }
    bool canSetTempo() const override { return true; }
    bool setTempo(double bpm) override {
        tempos.push_back(bpm);
        return true;
    }

    std::vector<double> tempos; // every setTempo the plan applied, in order
    TimelineDoc& doc;
    juce::AudioProcessorGraph& graph;
    AppUndoManager undo;
    MacroSet macros;
    int builds = 0, batches = 0, failOnBuild = 0;
    bool withoutEnvelope = false; // builds report no envelope node, as a host that could not make one would

private:
    juce::String add(const juce::String& type, const juce::var& params) {
        auto processor = AIStateMapper::createModule(type);
        if (auto* p = params.getDynamicObject())
            AIStateMapper::applyUntrustedParams(processor.get(), p);
        return AIStateMapper::ensureNodeUuid(graph.addNode(std::move(processor)).get());
    }
};

} // namespace synth
