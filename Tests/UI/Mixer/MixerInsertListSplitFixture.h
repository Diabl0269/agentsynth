#pragma once

// MixerInsertListSplitFixture.h -- a real graph, undo manager and canvas with one audio track whose chain splits
// (docs/mixer/mixer.md#parallel-paths), and the MixerInsertList showing that track's column. Header-only, shared by
// MixerInsertListSplitTests.cpp and MixerInsertListAddTests.cpp.

#include "AI/AIStateMapper/AIStateMapper.h"
#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "Mixer/MixerModel/MixerModel.h"
#include "Modules/ChannelStripModule.h"
#include "Modules/ModuleBase.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Mixer/MixerInsertList/MixerInsertList.h"
#include <gtest/gtest.h>

namespace mixer_split_test {

using NodeID = juce::AudioProcessorGraph::NodeID;

struct SplitListRig {
    AudioEngine engine;
    juce::AudioProcessorGraph& graph = engine.getGraph();
    AppUndoManager undo;
    GraphEditor editor{engine, &undo};
    synth::TimelineDoc doc;
    synth::ui::MixerInsertList list;
    NodeID source, strip;
    int mutations = 0;

    SplitListRig() {
        graph.setPlayConfigDetails(0, 2, 44100.0, 512);
        editor.setSize(1600, 900);
        synth::ui::MixerInsertList::resetSplitOpenStateForTest();
        synth::ui::MixerInsertList::resetRecentPicksForTest();
        const auto track = doc.addTrack(synth::TrackKind::Audio, "Lead");
        juce::String sourceUuid;
        source = add("Track Audio", 0, 400, &sourceUuid);
        strip = add("Channel Strip", 1400, 400);
        if (auto* m = dynamic_cast<ChannelStripModule*>(graph.getNodeForId(strip)->getProcessor()))
            m->setShape(ChannelStripModule::Shape::Stereo);
        doc.setTrackBinding(track, sourceUuid);
        list.configure(graph, undo, editor.getMacros(), editor);
        list.setSize(140, 400);
        list.onMutated = [this] {
            ++mutations;
            refresh();
        };
    }

    NodeID add(const juce::String& type, int x, int y, juce::String* uuidOut = nullptr) {
        auto node = graph.addNode(synth::AIStateMapper::createModule(type));
        const auto uuid = synth::AIStateMapper::ensureNodeUuid(node.get());
        node->properties.set("x", x);
        node->properties.set("y", y);
        if (uuidOut != nullptr)
            *uuidOut = uuid;
        return node->nodeID;
    }

    void wire(NodeID from, NodeID to) {
        auto* a = dynamic_cast<ModuleBase*>(graph.getNodeForId(from)->getProcessor());
        auto* b = dynamic_cast<ModuleBase*>(graph.getNodeForId(to)->getProcessor());
        graph.addConnection({{from, 0}, {to, 0}});
        graph.addConnection({{from, a->rightAudioLegChannel()}, {to, b->rightAudioLegChannel()}});
    }

    bool wired(NodeID from, NodeID to) const {
        for (const auto& c : graph.getConnections())
            if (c.source.nodeID == from && c.destination.nodeID == to)
                return true;
        return false;
    }

    synth::MixerColumn column() {
        const auto snapshot = synth::buildMixerSnapshot(graph, doc, editor.getMacros());
        for (const auto& c : snapshot.columns)
            if (c.nodeId == strip)
                return c;
        return {};
    }

    /** What the mixer panel does after every recorded edit: re-derive the column and hand it to the list. */
    void refresh() {
        editor.updateComponents();
        const auto c = column();
        list.setEntries(c.inserts, c.insertChainIsLinear, c.editOnCanvasTargetUuid, c.sourceNodeId, c.nodeId);
    }

    std::vector<juce::String> rows() const {
        std::vector<juce::String> out;
        for (int i = 0; i < list.getRowCount(); ++i)
            out.push_back(list.describeRow(i));
        return out;
    }

    bool bypassed(NodeID id) const {
        auto* m = dynamic_cast<ModuleBase*>(graph.getNodeForId(id)->getProcessor());
        return m != nullptr && m->isBypassed();
    }
};

/** Source -> Filter -> {Delay | Reverb} -> VCA -> strip: the ticket's own patch. */
struct TicketPatch : SplitListRig {
    NodeID filter, delay, reverb, vca;
    TicketPatch() {
        filter = add("Filter", 300, 400);
        delay = add("Delay", 600, 250);
        reverb = add("Reverb", 600, 550);
        vca = add("VCA", 900, 400);
        wire(source, filter);
        wire(filter, delay);
        wire(filter, reverb);
        wire(delay, vca);
        wire(reverb, vca);
        wire(vca, strip);
        refresh();
    }
};

} // namespace mixer_split_test
