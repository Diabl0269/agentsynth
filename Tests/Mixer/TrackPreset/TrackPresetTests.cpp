// Concern: FRO13 (P9-7, docs/mixer/track-presets.md) -- TrackPresetManager's save/load round trip: a
// captured track renders identically once reinserted, ids are renumbered so two inserts of the
// same preset never collide, and a hostile/malformed preset is refused whole (never partially
// applied) -- the SnippetManager::insertSnippet / ProjectBundle::load pairing's own contract,
// reused here via TrackPresetManager::insertTrackPreset's thin wrapper.
//
// The "outside modulator captured" and "walk stops at another channel's strip" assertions live in
// the sibling TrackPresetCaptureTests.cpp (they need TrackPresetRigCFT's LFO/otherChannelStrip);
// the "per-type default used by + Track" assertion lives in TrackPresetDefaultsTests.cpp (it needs
// a real MainComponent and the shared on-disk settings file).

#include "TrackPresetTestFixture.h"

#include <gtest/gtest.h>

TEST(TrackPreset, RoundTripRendersIdentically) {
    HostedPatchCFT reference;
    GraphEditor referenceEditor(reference.engine);
    const auto rig = buildSimpleTrackRigCFT(referenceEditor, reference.engine, reference.output);
    ASSERT_NE(rig.macro, nullptr);
    const auto referenceSamples = reference.render(4);

    auto preset = synth::TrackPresetManager::extractTrackPreset(
        reference.engine.getGraph(), referenceEditor.getMacros(), rig.macro->id, synth::TrackPresetKind::Audio, "RT");
    ASSERT_TRUE(preset.isObject());

    HostedPatchCFT fresh;
    std::vector<synth::Macro> outMacros;
    const auto added =
        synth::TrackPresetManager::insertTrackPreset(preset, fresh.engine.getGraph(), {0, 0}, &outMacros);
    ASSERT_FALSE(added.empty());
    ASSERT_NE(wireInsertedStripToOutputCFT(fresh.engine.getGraph(), added, fresh.output), nullptr);

    const auto freshSamples = fresh.render(4);
    expectIdenticalRendersCFT(referenceSamples, freshSamples);
}

TEST(TrackPreset, InsertingTwiceProducesTwoIndependentCopies) {
    HostedPatchCFT source;
    GraphEditor sourceEditor(source.engine);
    const auto rig = buildSimpleTrackRigCFT(sourceEditor, source.engine, source.output);
    ASSERT_NE(rig.macro, nullptr);

    auto preset = synth::TrackPresetManager::extractTrackPreset(source.engine.getGraph(), sourceEditor.getMacros(),
                                                                rig.macro->id, synth::TrackPresetKind::Audio, "RT2");
    ASSERT_TRUE(preset.isObject());

    juce::AudioProcessorGraph target;
    const auto first = synth::TrackPresetManager::insertTrackPreset(preset, target, {0, 0});
    const auto second = synth::TrackPresetManager::insertTrackPreset(preset, target, {900, 0});

    // Track In + Oscillator + Filter + the default Gate -> EQ -> Compressor -> Channel Strip chain
    // makeChannelFromNode folds the region into (T173a's default channel build-out).
    ASSERT_EQ(first.size(), 7u);
    EXPECT_EQ(second.size(), first.size());
    EXPECT_EQ(countNodesOfTypeCFT(target, ModuleType::Oscillator), 2);
    EXPECT_EQ(countNodesOfTypeCFT(target, ModuleType::Filter), 2);
    EXPECT_EQ(countNodesOfTypeCFT(target, ModuleType::ChannelStrip), 2);

    for (auto a : first)
        for (auto b : second)
            EXPECT_NE(a, b) << "the second insert must not collide with (or overwrite) the first";
}

TEST(TrackPreset, SmuggledTimelineKeyHasNoEffect) {
    // "timeline" is a reserved top-level key AIStateMapper::validatePatch's untrusted gate refuses
    // when it sees it directly (root CLAUDE.md's own tripwire) -- but insertTrackPreset never hands
    // validatePatch the raw file. It goes through SnippetManager::prepareForInsert first, which
    // rebuilds an entirely new var by copying only "nodes"/"connections"/"macros" -- any other
    // top-level key (a hand-edited "timeline" included) is dropped before validatePatch ever runs,
    // so smuggling one in has no effect at all: the valid node beside it still inserts normally.
    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    juce::Array<juce::var> nodes;
    juce::DynamicObject::Ptr node = new juce::DynamicObject();
    node->setProperty("id", 1);
    node->setProperty("type", "Oscillator");
    nodes.add(juce::var(node.get()));
    root->setProperty("nodes", nodes);
    root->setProperty("timeline", juce::var(new juce::DynamicObject()));

    juce::AudioProcessorGraph target;
    const auto added = synth::TrackPresetManager::insertTrackPreset(juce::var(root.get()), target, {0, 0});

    EXPECT_EQ(added.size(), 1u);
    EXPECT_EQ(countNodesOfTypeCFT(target, ModuleType::Oscillator), 1);
}

TEST(TrackPreset, RejectsMalformedPresetWithoutPartiallyApplying) {
    // Same "refused whole, not applied halfway" contract as
    // SnippetInsert.RejectsMalformedSnippetJSONWithoutPartiallyApplying -- a bad module type
    // anywhere in the file must sink the whole insert, including the otherwise-valid node beside it.
    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    juce::Array<juce::var> nodes;
    juce::DynamicObject::Ptr good = new juce::DynamicObject();
    good->setProperty("id", 1);
    good->setProperty("type", "Oscillator");
    nodes.add(juce::var(good.get()));
    juce::DynamicObject::Ptr bad = new juce::DynamicObject();
    bad->setProperty("id", 2);
    bad->setProperty("type", "NotARealModuleType");
    nodes.add(juce::var(bad.get()));
    root->setProperty("nodes", nodes);

    juce::AudioProcessorGraph target;
    const auto added = synth::TrackPresetManager::insertTrackPreset(juce::var(root.get()), target, {0, 0});

    EXPECT_TRUE(added.empty());
    EXPECT_EQ(target.getNumNodes(), 0) << "an invalid track preset must not add the valid node either";
}

TEST(TrackPreset, RejectsAnEmptyOrVoidPreset) {
    juce::AudioProcessorGraph target;
    EXPECT_TRUE(synth::TrackPresetManager::insertTrackPreset(juce::var(), target, {0, 0}).empty());
    EXPECT_EQ(target.getNumNodes(), 0);
}
