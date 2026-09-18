// Concern: FRO13 (P9-7, docs/mixer/track-presets.md#what-a-saved-preset-carries-beyond-the-box) -- what TrackPresetManager::extractTrackPreset
// captures beyond a channel macro's own members: an outside module modulating it through a port
// (collectOutsideModulatorsForTrackPreset's forward-seeded, backward/upstream walk) travels with
// the preset and is re-wired to the SAME modulation target on import; another channel's own strip
// (reached only by walking through a hidden AttenuverterModule) is never captured, matching the
// walk's own "that channel's business, not this preset's" stop rule.
//
// Uses TrackPresetRigCFT (TrackPresetTestFixture.h), which wires BOTH: the shared LFO -> this
// track's Filter Cutoff (the modulator that must be captured) and another channel's bare Channel
// Strip -> this track's Filter Resonance (the modulator that must NOT be).

#include "TrackPresetTestFixture.h"

#include <gtest/gtest.h>

namespace {

// Counts "type"==typeName entries in a preset's "nodes" array -- the walk-stop assertion needs to
// see how many strips travelled with the preset (exactly one: the track's own), not just how many
// nodes total.
int countNodesOfTypeInPresetCFT(const juce::var& preset, const juce::String& typeName) {
    auto* root = preset.getDynamicObject();
    if (root == nullptr)
        return 0;
    auto* nodes = root->getProperty("nodes").getArray();
    if (nodes == nullptr)
        return 0;
    int count = 0;
    for (const auto& n : *nodes)
        if (auto* obj = n.getDynamicObject())
            if (obj->getProperty("type").toString() == typeName)
                ++count;
    return count;
}

// Unlike modulatesCFT (which expects exactly one Attenuverter hop, optionally through ONE
// MacroInlet directly to `dest`), a track preset round trip goes through AIStateMapper's generic
// graphToJSON/applyJSONToGraph -- which represents ANY connection landing on a modulation-target
// channel as a "modulations" entry and always rebuilds a fresh Attenuverter for it on import, even
// for a leg that was a plain MacroInlet->dest wire in the ORIGINAL graph (a macro port is a pure
// pass-through with no modulation semantics of its own, so the port-to-destination leg picks up
// its own attenuverter on re-import). This walks forward through any number of Attenuverter/
// MacroInlet hops to prove the signal still reaches `dest`'s `channel`, without pinning down
// exactly how many hops the round trip happens to produce.
bool reachesModulationTargetCFT(juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::Node* source,
                                juce::AudioProcessorGraph::Node* dest, int channel, int maxHops = 6) {
    const auto connections = graph.getConnections();
    std::vector<juce::AudioProcessorGraph::NodeID> frontier{source->nodeID};
    for (int hop = 0; hop < maxHops && !frontier.empty(); ++hop) {
        std::vector<juce::AudioProcessorGraph::NodeID> next;
        for (const auto& id : frontier) {
            for (const auto& c : connections) {
                if (c.source.nodeID != id)
                    continue;
                if (c.destination.nodeID == dest->nodeID && c.destination.channelIndex == channel)
                    return true;
                auto* destProcessor = graph.getNodeForId(c.destination.nodeID);
                if (destProcessor != nullptr && (isModuleOfTypeCFT(destProcessor, ModuleType::Attenuverter) ||
                                                 isModuleOfTypeCFT(destProcessor, ModuleType::MacroInlet)))
                    next.push_back(c.destination.nodeID);
            }
        }
        frontier = std::move(next);
    }
    return false;
}

} // namespace

TEST(TrackPresetCapture, OutsideModulatorCapturedAndRewiredOnImport) {
    HostedPatchCFT patch;
    GraphEditor editor(patch.engine);
    const auto rig = buildTrackPresetRigCFT(editor, patch.engine, patch.output);
    ASSERT_NE(rig.macro, nullptr);
    ASSERT_GE(rig.cutoffChannel, 0);

    auto preset = synth::TrackPresetManager::extractTrackPreset(patch.engine.getGraph(), editor.getMacros(),
                                                                rig.macro->id, synth::TrackPresetKind::Audio, "Cap");
    ASSERT_TRUE(preset.isObject());
    EXPECT_EQ(countNodesOfTypeInPresetCFT(preset, "LFO"), 1)
        << "the shared LFO feeds this channel's Cutoff through a port -- it must travel with the preset";

    juce::AudioProcessorGraph target;
    const auto added = synth::TrackPresetManager::insertTrackPreset(preset, target, {0, 0});
    ASSERT_FALSE(added.empty());

    auto* lfoInTarget = findNodeOfTypeCFT(target, ModuleType::LFO);
    auto* filterInTarget = findNodeOfTypeCFT(target, ModuleType::Filter);
    ASSERT_NE(lfoInTarget, nullptr);
    ASSERT_NE(filterInTarget, nullptr);
    EXPECT_TRUE(reachesModulationTargetCFT(target, lfoInTarget, filterInTarget, rig.cutoffChannel))
        << "the re-inserted LFO must still modulate the re-inserted Filter's Cutoff, through "
           "freshly-rebuilt Attenuverter(s) (never a captured one -- attenuverters are never captured)";
}

TEST(TrackPresetCapture, SoloScrubbedFromCapturedChannelStrip) {
    // Root CLAUDE.md tripwire: an imported soloed_=true would silence the whole mix render-wide.
    // extractTrackPreset forces includeExtraState=true so shape/gain/pan travel with the preset,
    // but must scrub "solo" out of the captured Channel Strip's state before it's ever written --
    // this is the one property that must never survive into a saved/inserted preset.
    HostedPatchCFT patch;
    GraphEditor editor(patch.engine);
    const auto rig = buildSimpleTrackRigCFT(editor, patch.engine, patch.output);
    ASSERT_NE(rig.macro, nullptr);

    auto* stripNode = findNodeOfTypeCFT(patch.engine.getGraph(), ModuleType::ChannelStrip);
    ASSERT_NE(stripNode, nullptr);
    auto* stripModule = dynamic_cast<ChannelStripModule*>(stripNode->getProcessor());
    ASSERT_NE(stripModule, nullptr);
    stripModule->setSoloed(true);

    auto preset = synth::TrackPresetManager::extractTrackPreset(
        patch.engine.getGraph(), editor.getMacros(), rig.macro->id, synth::TrackPresetKind::Audio, "SoloScrub");
    ASSERT_TRUE(preset.isObject());
    auto* root = preset.getDynamicObject();
    ASSERT_NE(root, nullptr);
    auto* nodes = root->getProperty("nodes").getArray();
    ASSERT_NE(nodes, nullptr);

    bool foundStrip = false;
    for (const auto& n : *nodes) {
        auto* obj = n.getDynamicObject();
        if (obj == nullptr || obj->getProperty("type").toString() != "Channel Strip")
            continue;
        foundStrip = true;
        auto* state = obj->getProperty("state").getDynamicObject();
        ASSERT_NE(state, nullptr) << "includeExtraState=true must still carry shape/gain/pan state";
        EXPECT_FALSE(state->hasProperty("solo"))
            << "an imported soloed_=true would silence the whole mix render-wide (root CLAUDE.md tripwire)";
    }
    EXPECT_TRUE(foundStrip);
}

TEST(TrackPresetCapture, IsBusScrubbedFromCapturedChannelStrip) {
    // FRO98 follow-up to the solo scrub above: a preset captured from a bus strip must not carry
    // "isBus" into wherever it's inserted, or it badges an ordinary track channel as BUS (docs/mixer/sends-and-buses.md).
    HostedPatchCFT patch;
    GraphEditor editor(patch.engine);
    const auto rig = buildSimpleTrackRigCFT(editor, patch.engine, patch.output);
    ASSERT_NE(rig.macro, nullptr);

    auto* stripNode = findNodeOfTypeCFT(patch.engine.getGraph(), ModuleType::ChannelStrip);
    ASSERT_NE(stripNode, nullptr);
    auto* stripModule = dynamic_cast<ChannelStripModule*>(stripNode->getProcessor());
    ASSERT_NE(stripModule, nullptr);
    stripModule->setIsBus(true);
    ASSERT_TRUE(stripModule->getExtraState().getDynamicObject()->hasProperty("isBus"))
        << "getExtraState() always writes \"isBus\" -- if this ever stops being true, the "
           "EXPECT_FALSE below would pass vacuously instead of proving the scrub ran";

    auto preset = synth::TrackPresetManager::extractTrackPreset(
        patch.engine.getGraph(), editor.getMacros(), rig.macro->id, synth::TrackPresetKind::Audio, "BusScrub");
    ASSERT_TRUE(preset.isObject());
    auto* root = preset.getDynamicObject();
    ASSERT_NE(root, nullptr);
    auto* nodes = root->getProperty("nodes").getArray();
    ASSERT_NE(nodes, nullptr);

    bool foundStrip = false;
    for (const auto& n : *nodes) {
        auto* obj = n.getDynamicObject();
        if (obj == nullptr || obj->getProperty("type").toString() != "Channel Strip")
            continue;
        foundStrip = true;
        auto* state = obj->getProperty("state").getDynamicObject();
        ASSERT_NE(state, nullptr) << "includeExtraState=true must still carry shape/gain/pan state";
        EXPECT_FALSE(state->hasProperty("isBus"))
            << "an imported isBus=true would badge an ordinary track channel as BUS wherever the preset lands";
    }
    EXPECT_TRUE(foundStrip);
}

TEST(TrackPresetCapture, SendsScrubbedFromCapturedChannelStrip) {
    // FRO98 follow-up to the solo scrub above: a preset captured from a strip with configured
    // sends must not carry "sends" slot state -- a send's target is a graph edge that is never
    // stored (docs/mixer/sends-and-buses.md), so a captured slot would restore with no cable, showing a "No target" row.
    HostedPatchCFT patch;
    GraphEditor editor(patch.engine);
    const auto rig = buildSimpleTrackRigCFT(editor, patch.engine, patch.output);
    ASSERT_NE(rig.macro, nullptr);

    auto* stripNode = findNodeOfTypeCFT(patch.engine.getGraph(), ModuleType::ChannelStrip);
    ASSERT_NE(stripNode, nullptr);
    auto* stripModule = dynamic_cast<ChannelStripModule*>(stripNode->getProcessor());
    ASSERT_NE(stripModule, nullptr);
    ASSERT_GE(stripModule->addSend(), 0) << "test setup: activating a send slot must succeed";
    ASSERT_TRUE(stripModule->getExtraState().getDynamicObject()->getProperty("sends").getArray() != nullptr &&
                !stripModule->getExtraState().getDynamicObject()->getProperty("sends").getArray()->isEmpty())
        << "getExtraState() must actually carry the active send, or the EXPECT_FALSE below would "
           "pass vacuously instead of proving the scrub ran";

    auto preset = synth::TrackPresetManager::extractTrackPreset(
        patch.engine.getGraph(), editor.getMacros(), rig.macro->id, synth::TrackPresetKind::Audio, "SendsScrub");
    ASSERT_TRUE(preset.isObject());
    auto* root = preset.getDynamicObject();
    ASSERT_NE(root, nullptr);
    auto* nodes = root->getProperty("nodes").getArray();
    ASSERT_NE(nodes, nullptr);

    bool foundStrip = false;
    for (const auto& n : *nodes) {
        auto* obj = n.getDynamicObject();
        if (obj == nullptr || obj->getProperty("type").toString() != "Channel Strip")
            continue;
        foundStrip = true;
        auto* state = obj->getProperty("state").getDynamicObject();
        ASSERT_NE(state, nullptr) << "includeExtraState=true must still carry shape/gain/pan state";
        EXPECT_FALSE(state->hasProperty("sends"))
            << "a captured send slot with no re-resolved cable target would show a \"No target\" row on insert";
    }
    EXPECT_TRUE(foundStrip);
}

TEST(TrackPresetCapture, MacroMenuOffersTrackPresetItemsOnlyForAChannelMacro) {
    // GraphEditorMacroPrompts.cpp's buildMacroMenu gates "Save Track as Preset..."/"Set as Default
    // Track Preset" on synth::isChannelMacro(*macro, graph) -- omitted entirely (not merely
    // disabled) for an ordinary group, same "Mute Macro" omit-when-meaningless precedent. Neither
    // the header-menu tests (StubHost, never reaches isChannelMacro) nor the other
    // TrackPresetCapture tests (only hit isChannelMacro's "false" branch, via the other-channel
    // stop rule) actually exercise this gate -- this is the one test that does, on both sides.
    HostedPatchCFT patch;
    GraphEditor editor(patch.engine);
    const auto rig = buildSimpleTrackRigCFT(editor, patch.engine, patch.output);
    ASSERT_NE(rig.macro, nullptr);

    const auto channelMenu = editor.buildMacroMenu(rig.macro->id);
    EXPECT_NE(findMenuItemByTextCFT(channelMenu, "Save Track as Preset..."), nullptr);
    EXPECT_NE(findMenuItemByTextCFT(channelMenu, "Set as Default Track Preset"), nullptr);

    juce::String unused;
    auto* a = addPlainNodeCFT(patch.engine.getGraph(), "Oscillator", {1200, 0}, unused);
    auto* b = addPlainNodeCFT(patch.engine.getGraph(), "Filter", {1400, 0}, unused);
    editor.setSelectedNodes({a->nodeID, b->nodeID});
    const auto plainMacroId = editor.groupSelectionIntoMacro();
    ASSERT_FALSE(plainMacroId.isEmpty());

    const auto plainMenu = editor.buildMacroMenu(plainMacroId);
    EXPECT_EQ(findMenuItemByTextCFT(plainMenu, "Save Track as Preset..."), nullptr)
        << "an ordinary (non-channel) macro must not offer track-preset actions at all";
    EXPECT_EQ(findMenuItemByTextCFT(plainMenu, "Set as Default Track Preset"), nullptr);
}

TEST(TrackPresetCapture, WalkStopsAtAnotherChannelsStrip) {
    HostedPatchCFT patch;
    GraphEditor editor(patch.engine);
    const auto rig = buildTrackPresetRigCFT(editor, patch.engine, patch.output);
    ASSERT_NE(rig.macro, nullptr);
    ASSERT_GE(rig.resonanceChannel, 0);

    auto preset = synth::TrackPresetManager::extractTrackPreset(patch.engine.getGraph(), editor.getMacros(),
                                                                rig.macro->id, synth::TrackPresetKind::Audio, "Cap2");
    ASSERT_TRUE(preset.isObject());

    // Exactly one Channel Strip may travel with the preset -- the track's own. otherChannelStrip
    // modulates this track's Resonance through its own hidden Attenuverter (the walk enters that
    // attenuverter looking for the real modulator behind it), but isStrip() must stop the walk
    // there rather than adding otherChannelStrip itself.
    EXPECT_EQ(countNodesOfTypeInPresetCFT(preset, "Channel Strip"), 1);

    juce::AudioProcessorGraph target;
    const auto added = synth::TrackPresetManager::insertTrackPreset(preset, target, {0, 0});
    ASSERT_FALSE(added.empty());
    EXPECT_EQ(countNodesOfTypeCFT(target, ModuleType::ChannelStrip), 1)
        << "another channel's strip must never be reconstructed alongside this preset's own";
}
