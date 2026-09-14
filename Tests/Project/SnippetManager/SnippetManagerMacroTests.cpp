// Concern: macros (P8-12 name/colour/membership, P8-15 ports) surviving extract-and-insert,
// including double-insert producing independent copies and the InsertedMacroSet var round trip.
#include "Project/SnippetManager/SnippetManagerTestHelpers.h"

// ---------------------------------------------------------------------------------------
// Macros (P8-12 name/colour/membership, P8-15 ports) — T117
// ---------------------------------------------------------------------------------------

static juce::AudioProcessorGraph::Node::Ptr
addAtWithUuid(juce::AudioProcessorGraph& graph, std::unique_ptr<juce::AudioProcessor> processor, int x, int y) {
    auto node = addAt(graph, std::move(processor), x, y);
    node->properties.set("uuid", juce::Uuid().toDashedString());
    return node;
}

TEST(SnippetMacro, NameColourAndMembershipSurviveExtractAndInsert) {
    juce::AudioProcessorGraph graph;
    auto osc = addAtWithUuid(graph, std::make_unique<OscillatorModule>(), 0, 0);
    auto filter = addAtWithUuid(graph, std::make_unique<FilterModule>(), 300, 0);
    const juce::String origOscUuid = osc->properties["uuid"].toString();
    const juce::String origFilterUuid = filter->properties["uuid"].toString();

    MacroSet macros;
    Macro macro;
    macro.name = "Filter Section";
    macro.colour = juce::Colour(0xffaa3344);
    macro.collapsed = true;
    macro.bounds = {50, 50, 200, 120};
    macro.members = {origOscUuid, origFilterUuid};
    macros.add(macro);

    auto snippet = SnippetManager::extractSnippet(graph, {osc->nodeID, filter->nodeID}, "Grouped",
                                                  /*includeExtraState=*/false, macros);

    juce::AudioProcessorGraph target;
    std::vector<Macro> addedMacros;
    auto added = SnippetManager::insertSnippet(snippet, target, {0, 0}, /*includeExtraState=*/false, &addedMacros);
    ASSERT_EQ(added.size(), 2u);
    ASSERT_EQ(addedMacros.size(), 1u);

    EXPECT_EQ(addedMacros[0].name, "Filter Section");
    EXPECT_EQ(addedMacros[0].colour, juce::Colour(0xffaa3344));
    EXPECT_TRUE(addedMacros[0].collapsed);
    ASSERT_EQ(addedMacros[0].members.size(), 2u);
    EXPECT_EQ(std::find(addedMacros[0].members.begin(), addedMacros[0].members.end(), origOscUuid),
              addedMacros[0].members.end())
        << "pasted copy must get fresh uuids, not reuse the originals";
}

TEST(SnippetMacro, PortsSurviveExtractAndInsertWithResolvedUuidsAndKind) {
    juce::AudioProcessorGraph graph;
    auto osc = addAtWithUuid(graph, std::make_unique<OscillatorModule>(), 0, 0);
    auto inlet = addAtWithUuid(graph, std::make_unique<MacroInletModule>(), 300, 0);
    const juce::String origInletUuid = inlet->properties["uuid"].toString();

    MacroSet macros;
    Macro macro;
    macro.name = "With Port";
    macro.members = {osc->properties["uuid"].toString(), origInletUuid};

    MacroPort port;
    port.nodeUuid = origInletUuid;
    port.isInput = true;
    port.name = "Pitch In";
    port.order = 3;
    port.kind = MacroPortKind::AudioCV;
    port.colour = juce::Colour(0xff112233);
    macro.ports.push_back(port);
    macros.add(macro);

    auto snippet = SnippetManager::extractSnippet(graph, {osc->nodeID, inlet->nodeID}, "PortedMacro",
                                                  /*includeExtraState=*/false, macros);

    juce::AudioProcessorGraph target;
    std::vector<Macro> addedMacros;
    auto added = SnippetManager::insertSnippet(snippet, target, {0, 0}, /*includeExtraState=*/false, &addedMacros);
    ASSERT_EQ(added.size(), 2u);
    ASSERT_EQ(addedMacros.size(), 1u);
    ASSERT_EQ(addedMacros[0].ports.size(), 1u) << "the configured port must not be silently dropped";

    const auto& newPort = addedMacros[0].ports[0];
    EXPECT_EQ(newPort.name, "Pitch In");
    EXPECT_TRUE(newPort.isInput);
    EXPECT_EQ(newPort.order, 3);
    EXPECT_EQ(newPort.kind, MacroPortKind::AudioCV);
    ASSERT_TRUE(newPort.colour.has_value());
    EXPECT_EQ(*newPort.colour, juce::Colour(0xff112233));

    EXPECT_NE(newPort.nodeUuid, origInletUuid) << "must front the pasted copy's node, not the original's";
    EXPECT_NE(std::find(addedMacros[0].members.begin(), addedMacros[0].members.end(), newPort.nodeUuid),
              addedMacros[0].members.end())
        << "a port's nodeUuid must be one of the pasted macro's own members (MacroSet's own invariant)";
}

TEST(SnippetMacro, InsertingTwiceProducesTwoIndependentMacrosWithDistinctIds) {
    juce::AudioProcessorGraph graph;
    auto osc = addAtWithUuid(graph, std::make_unique<OscillatorModule>(), 0, 0);
    auto filter = addAtWithUuid(graph, std::make_unique<FilterModule>(), 300, 0);

    MacroSet sourceMacros;
    Macro macro;
    macro.name = "Pair";
    macro.members = {osc->properties["uuid"].toString(), filter->properties["uuid"].toString()};
    sourceMacros.add(macro);

    auto snippet = SnippetManager::extractSnippet(graph, {osc->nodeID, filter->nodeID}, "Pair",
                                                  /*includeExtraState=*/false, sourceMacros);

    juce::AudioProcessorGraph target;
    MacroSet targetMacros;

    std::vector<Macro> first;
    ASSERT_EQ(SnippetManager::insertSnippet(snippet, target, {0, 0}, false, &first).size(), 2u);
    ASSERT_EQ(first.size(), 1u);
    auto& storedFirst = targetMacros.add(first[0]);
    const auto firstId = storedFirst.id;

    std::vector<Macro> second;
    ASSERT_EQ(SnippetManager::insertSnippet(snippet, target, {900, 0}, false, &second).size(), 2u);
    ASSERT_EQ(second.size(), 1u);
    auto& storedSecond = targetMacros.add(second[0]);
    const auto secondId = storedSecond.id;

    EXPECT_EQ(targetMacros.size(), 2);
    EXPECT_FALSE(firstId.isEmpty());
    EXPECT_FALSE(secondId.isEmpty());
    EXPECT_NE(firstId, secondId);
    for (const auto& uuid : storedFirst.members)
        EXPECT_EQ(std::find(storedSecond.members.begin(), storedSecond.members.end(), uuid), storedSecond.members.end())
            << "the two pasted copies must not share member uuids";
}

TEST(SnippetMacro, InsertedMacroSetRoundTripsThroughToVarFromVar) {
    // Regression guard for the class of corruption a broken member/port pairing causes: it does not
    // show up on the macro that was just inserted, only the next time the project (or a snippet
    // carrying this macro again) round-trips through MacroSet::toVar()/fromVar() — at which point
    // fromVar rejects the WHOLE set, not just the one bad macro (MacroSet.h's own doc comment).
    juce::AudioProcessorGraph graph;
    auto osc = addAtWithUuid(graph, std::make_unique<OscillatorModule>(), 0, 0);
    auto inlet = addAtWithUuid(graph, std::make_unique<MacroInletModule>(), 300, 0);
    auto outlet = addAtWithUuid(graph, std::make_unique<MacroOutletModule>(), 600, 0);

    MacroSet macros;
    Macro macro;
    macro.name = "Round Trip";
    macro.members = {osc->properties["uuid"].toString(), inlet->properties["uuid"].toString(),
                     outlet->properties["uuid"].toString()};

    MacroPort in;
    in.nodeUuid = inlet->properties["uuid"].toString();
    in.isInput = true;
    in.name = "In";
    in.kind = MacroPortKind::AudioCV;
    macro.ports.push_back(in);

    MacroPort out;
    out.nodeUuid = outlet->properties["uuid"].toString();
    out.isInput = false;
    out.name = "Out";
    out.kind = MacroPortKind::AudioCV;
    macro.ports.push_back(out);

    macros.add(macro);

    auto snippet = SnippetManager::extractSnippet(graph, {osc->nodeID, inlet->nodeID, outlet->nodeID}, "RT",
                                                  /*includeExtraState=*/false, macros);

    juce::AudioProcessorGraph target;
    MacroSet targetMacros;
    std::vector<Macro> addedMacros;
    ASSERT_EQ(SnippetManager::insertSnippet(snippet, target, {0, 0}, false, &addedMacros).size(), 3u);
    ASSERT_EQ(addedMacros.size(), 1u);
    targetMacros.add(addedMacros[0]);

    auto serialised = targetMacros.toVar();
    MacroSet reloaded;
    ASSERT_TRUE(reloaded.fromVar(serialised)) << "a broken member/port pairing must never reach fromVar";
    ASSERT_EQ(reloaded.size(), 1);
    EXPECT_EQ(reloaded.getAll()[0].ports.size(), 2u);
}

TEST(SnippetMacro, SaveLoadInsertRoundTripsNameColourAndPorts) {
    auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("agentsynth-macro-snippet-tests");
    dir.deleteRecursively();
    dir.createDirectory();

    juce::AudioProcessorGraph graph;
    auto osc = addAtWithUuid(graph, std::make_unique<OscillatorModule>(), 0, 0);
    auto inlet = addAtWithUuid(graph, std::make_unique<MacroInletModule>(), 300, 0);

    MacroSet macros;
    Macro macro;
    macro.name = "Disk Round Trip";
    macro.colour = juce::Colour(0xff2299ee);
    macro.members = {osc->properties["uuid"].toString(), inlet->properties["uuid"].toString()};

    MacroPort port;
    port.nodeUuid = inlet->properties["uuid"].toString();
    port.isInput = true;
    port.name = "Mod In";
    port.kind = MacroPortKind::AudioCV;
    macro.ports.push_back(port);
    macros.add(macro);

    auto snippet = SnippetManager::extractSnippet(graph, {osc->nodeID, inlet->nodeID}, "OnDisk",
                                                  /*includeExtraState=*/false, macros);
    ASSERT_TRUE(SnippetManager::saveSnippet(dir, "OnDisk", snippet));

    auto loaded = SnippetManager::loadSnippet(SnippetManager::fileForName(dir, "OnDisk"));
    ASSERT_TRUE(loaded.isObject());

    juce::AudioProcessorGraph target;
    std::vector<Macro> addedMacros;
    auto added = SnippetManager::insertSnippet(loaded, target, {0, 0}, /*includeExtraState=*/false, &addedMacros);
    ASSERT_EQ(added.size(), 2u);
    ASSERT_EQ(addedMacros.size(), 1u);

    EXPECT_EQ(addedMacros[0].name, "Disk Round Trip");
    EXPECT_EQ(addedMacros[0].colour, juce::Colour(0xff2299ee));
    ASSERT_EQ(addedMacros[0].ports.size(), 1u);
    EXPECT_EQ(addedMacros[0].ports[0].name, "Mod In");

    dir.deleteRecursively();
}

// ---------------------------------------------------------------------------------------
// Name sanitisation + drag payloads
// ---------------------------------------------------------------------------------------
