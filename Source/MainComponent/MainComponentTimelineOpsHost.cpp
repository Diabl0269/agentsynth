// MainComponentTimelineOpsHost.cpp -- the app side of a timelineOps batch that builds graph-side
// tracks (docs/ai/timeline-ops.md#addinstrumenttrack). Installed on aiService in
// MainComponentSetup.cpp; TimelineOps::apply calls recordBatch around the whole batch and
// addInstrumentTrack from inside it.
#include "MainComponentTimelineOpsHost.h"

#include "AI/AIStateMapper/AIStateMapper.h"
#include "AudioEngine/AudioEngine.h"
#include "MainComponent.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/NewModulePlacement/NewModulePlacement.h"

// The same build "+ Track -> Instrument" runs (MainComponent::buildInstrumentTrackBody), with the
// exact name the op asked for and WITHOUT the user's default-preset lookup: the preview the user
// agreed to describes this chain, so a preset must not silently replace it. The model can bind
// nothing but the Track In this creates -- "Track In" itself stays non-authorable in patches.
std::optional<synth::InstrumentTrackBuildResult>
MainComponentTimelineOpsHost::addInstrumentTrack(const juce::String& name, const juce::String& instrumentType,
                                                 bool poly, const std::vector<synth::InstrumentTrackInsert>& inserts,
                                                 const juce::var& envelopeParams) {
    auto instrument = synth::AIStateMapper::createModule(instrumentType);
    if (instrument == nullptr)
        return std::nullopt; // nothing created yet, so nothing to remove
    auto staged = std::make_shared<std::unique_ptr<juce::AudioProcessor>>(std::move(instrument));
    return owner_.buildInstrumentTrackBody(staged, name, poly, inserts, envelopeParams);
}

// ONE undo step over graph, timeline and macros -- the transaction addInstrumentTrack's own menu
// flow opens -- then the reconcile every graph-changing track flow runs afterwards.
bool MainComponentTimelineOpsHost::recordBatch(const std::function<void()>& mutation) {
    const bool pushed = owner_.undoManager.recordGraphTimelineAndMacroChange(
        owner_.audioEngine.getGraph(), owner_.timelineDoc, owner_.graphEditor.getMacros(), mutation);
    owner_.reconcileTimelineAfterGraphChange();
    return pushed;
}

// The doc an AIIntegrationService edit plan writes to from inside recordBatch: the app's one live
// timeline, the same one recordBatch snapshots.
synth::TimelineDoc* MainComponentTimelineOpsHost::editableTimelineDoc() { return &owner_.timelineDoc; }

// Inside recordBatch, after the plan's patch: the nodes it added without a "position" land beside what they connect
// to (docs/ai/timeline-ops.md#where-things-land), so their spots and any make-room are part of the one undo step.
void MainComponentTimelineOpsHost::placeNewModules(const std::vector<juce::AudioProcessorGraph::NodeID>& created) {
    synth::placeNewModulesBesideConnections(owner_.graphEditor, created);
}
