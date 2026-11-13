// Concern: macros (name/colour/membership, ports) surviving extract-and-insert,
// including double-insert producing independent copies and the InsertedMacroSet var round trip.
#include "SnippetManagerTestHelpers.h"

// ---------------------------------------------------------------------------------------
// Macros (name/colour/membership, ports)
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
    // MacroSet::add() returns the new macro's id BY VALUE, not a reference into the stored copy
    // (it used to return `Macro&`, and holding that across the SECOND add() below — which
    // reallocates the underlying storage — was an ASAN heap-use-after-free). Look macros back up
    // via find() only after every add() call has already happened.
    const auto firstId = targetMacros.add(first[0]);

    std::vector<Macro> second;
    ASSERT_EQ(SnippetManager::insertSnippet(snippet, target, {900, 0}, false, &second).size(), 2u);
    ASSERT_EQ(second.size(), 1u);
    const auto secondId = targetMacros.add(second[0]);

    EXPECT_EQ(targetMacros.size(), 2);
    EXPECT_FALSE(firstId.isEmpty());
    EXPECT_FALSE(secondId.isEmpty());
    EXPECT_NE(firstId, secondId);

    const auto* storedFirst = targetMacros.find(firstId);
    const auto* storedSecond = targetMacros.find(secondId);
    ASSERT_NE(storedFirst, nullptr);
    ASSERT_NE(storedSecond, nullptr);
    for (const auto& uuid : storedFirst->members)
        EXPECT_EQ(std::find(storedSecond->members.begin(), storedSecond->members.end(), uuid),
                  storedSecond->members.end())
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

// ---------------------------------------------------------------------------------------
// Nested macros
// ---------------------------------------------------------------------------------------

namespace {

// Inserts `snippet` into a fresh graph and feeds the resulting macros through a MacroSet the way
// every caller does (add each in order), so a broken parent link would show up as a lost hierarchy.
MacroSet insertIntoMacroSet(const juce::var& snippet, size_t expectedNodes) {
    juce::AudioProcessorGraph target;
    std::vector<Macro> added;
    EXPECT_EQ(SnippetManager::insertSnippet(snippet, target, {0, 0}, false, &added).size(), expectedNodes);
    MacroSet set;
    for (auto& m : added)
        set.add(m);
    return set;
}

const Macro* findMacroNamed(const MacroSet& set, const juce::String& name) {
    for (const auto& m : set.getAll())
        if (m.name == name)
            return &m;
    return nullptr;
}

} // namespace

TEST(SnippetMacro, CopyPasteKeepsANestedChildMacroUnderItsParent) {
    juce::AudioProcessorGraph graph;
    auto osc = addAtWithUuid(graph, std::make_unique<OscillatorModule>(), 0, 0);
    auto filter = addAtWithUuid(graph, std::make_unique<FilterModule>(), 300, 0);
    auto vca = addAtWithUuid(graph, std::make_unique<VCAModule>(), 600, 0);

    MacroSet macros;
    Macro inner;
    inner.name = "Inner";
    inner.members = {osc->properties["uuid"].toString(), filter->properties["uuid"].toString()};
    const auto innerId = macros.add(inner);
    Macro outer;
    outer.name = "Outer";
    outer.members = {vca->properties["uuid"].toString()};
    const auto outerId = macros.add(outer);
    ASSERT_TRUE(macros.setParent(innerId, outerId));

    auto snippet = SnippetManager::extractSnippet(graph, {osc->nodeID, filter->nodeID, vca->nodeID}, "Nested",
                                                  /*includeExtraState=*/false, macros);
    const auto pasted = insertIntoMacroSet(snippet, 3);

    ASSERT_EQ(pasted.size(), 2);
    const auto* pastedInner = findMacroNamed(pasted, "Inner");
    const auto* pastedOuter = findMacroNamed(pasted, "Outer");
    ASSERT_NE(pastedInner, nullptr);
    ASSERT_NE(pastedOuter, nullptr);
    EXPECT_EQ(pastedInner->parentId, pastedOuter->id);
    EXPECT_TRUE(pastedOuter->parentId.isEmpty());
    EXPECT_EQ(pastedInner->members.size(), 2u);
    EXPECT_EQ(pastedOuter->members.size(), 1u) << "a parent's own members stay direct, the child's are not folded in";
    EXPECT_EQ(pasted.descendantMembers(pastedOuter->id).size(), 3u);
    EXPECT_NE(pastedOuter->id, outerId) << "a pasted macro gets a fresh id";

    MacroSet reloaded;
    EXPECT_TRUE(reloaded.fromVar(pasted.toVar()));
}

TEST(SnippetMacro, ContainerOnlyParentWithNoDirectMembersRoundTripsThroughSaveAndLoad) {
    auto dir =
        juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("agentsynth-nested-snippet-tests");
    dir.deleteRecursively();
    dir.createDirectory();

    juce::AudioProcessorGraph graph;
    auto osc = addAtWithUuid(graph, std::make_unique<OscillatorModule>(), 0, 0);
    auto filter = addAtWithUuid(graph, std::make_unique<FilterModule>(), 300, 0);
    auto vca = addAtWithUuid(graph, std::make_unique<VCAModule>(), 600, 0);
    auto lfo = addAtWithUuid(graph, std::make_unique<LFOModule>(), 900, 0);

    MacroSet macros;
    Macro a;
    a.name = "A";
    a.members = {osc->properties["uuid"].toString(), filter->properties["uuid"].toString()};
    const auto aId = macros.add(a);
    Macro b;
    b.name = "B";
    b.members = {vca->properties["uuid"].toString()}; // a single-member child
    const auto bId = macros.add(b);
    Macro container;
    container.name = "Container"; // no direct members at all
    const auto containerId = macros.add(container);
    ASSERT_TRUE(macros.setParent(aId, containerId));
    ASSERT_TRUE(macros.setParent(bId, containerId));
    // A macro that is not inside the selection is not captured.
    Macro partial;
    partial.name = "Partial";
    partial.members = {lfo->properties["uuid"].toString()};
    macros.add(partial);

    auto snippet = SnippetManager::extractSnippet(graph, {osc->nodeID, filter->nodeID, vca->nodeID}, "Container",
                                                  /*includeExtraState=*/false, macros);
    ASSERT_TRUE(SnippetManager::saveSnippet(dir, "Container", snippet));
    auto loaded = SnippetManager::loadSnippet(SnippetManager::fileForName(dir, "Container"));
    ASSERT_TRUE(loaded.isObject());

    const auto pasted = insertIntoMacroSet(loaded, 3);
    ASSERT_EQ(pasted.size(), 3);
    const auto* pastedContainer = findMacroNamed(pasted, "Container");
    ASSERT_NE(pastedContainer, nullptr);
    EXPECT_TRUE(pastedContainer->members.empty());
    EXPECT_EQ(pasted.childrenOf(pastedContainer->id).size(), 2u);
    EXPECT_EQ(findMacroNamed(pasted, "B")->members.size(), 1u);
    EXPECT_EQ(findMacroNamed(pasted, "Partial"), nullptr);
    MacroSet reloaded;
    EXPECT_TRUE(reloaded.fromVar(pasted.toVar()));

    dir.deleteRecursively();
}

TEST(SnippetMacro, ChildWhoseParentIsOnlyPartlySelectedPastesAsTopLevel) {
    juce::AudioProcessorGraph graph;
    auto osc = addAtWithUuid(graph, std::make_unique<OscillatorModule>(), 0, 0);
    auto filter = addAtWithUuid(graph, std::make_unique<FilterModule>(), 300, 0);
    auto vca = addAtWithUuid(graph, std::make_unique<VCAModule>(), 600, 0);

    MacroSet macros;
    Macro inner;
    inner.name = "Inner";
    inner.members = {osc->properties["uuid"].toString(), filter->properties["uuid"].toString()};
    const auto innerId = macros.add(inner);
    Macro outer;
    outer.name = "Outer";
    outer.members = {vca->properties["uuid"].toString()};
    const auto outerId = macros.add(outer);
    ASSERT_TRUE(macros.setParent(innerId, outerId));

    // The VCA (the parent's own member) is left out, so the parent is not fully contained.
    auto snippet = SnippetManager::extractSnippet(graph, {osc->nodeID, filter->nodeID}, "Half",
                                                  /*includeExtraState=*/false, macros);
    const auto pasted = insertIntoMacroSet(snippet, 2);
    ASSERT_EQ(pasted.size(), 1);
    EXPECT_EQ(pasted.getAll()[0].name, "Inner");
    EXPECT_TRUE(pasted.getAll()[0].parentId.isEmpty());
}

TEST(SnippetMacro, HandEditedSnippetWithACyclicParentLinkNeverProducesAnUnloadableSet) {
    juce::AudioProcessorGraph graph;
    auto osc = addAtWithUuid(graph, std::make_unique<OscillatorModule>(), 0, 0);
    auto filter = addAtWithUuid(graph, std::make_unique<FilterModule>(), 300, 0);
    auto vca = addAtWithUuid(graph, std::make_unique<VCAModule>(), 600, 0);
    auto lfo = addAtWithUuid(graph, std::make_unique<LFOModule>(), 900, 0);

    MacroSet macros;
    Macro a;
    a.name = "A";
    a.members = {osc->properties["uuid"].toString(), filter->properties["uuid"].toString()};
    macros.add(a);
    Macro b;
    b.name = "B";
    b.members = {vca->properties["uuid"].toString(), lfo->properties["uuid"].toString()};
    macros.add(b);
    auto snippet = SnippetManager::extractSnippet(graph, {osc->nodeID, filter->nodeID, vca->nodeID, lfo->nodeID},
                                                  "Cycle", /*includeExtraState=*/false, macros);
    auto* list = snippet.getDynamicObject()->getProperty("macros").getArray();
    ASSERT_NE(list, nullptr);
    ASSERT_EQ(list->size(), 2);
    (*list)[0].getDynamicObject()->setProperty("parent", 1);
    (*list)[1].getDynamicObject()->setProperty("parent", 0);

    const auto pasted = insertIntoMacroSet(snippet, 4);
    EXPECT_EQ(pasted.size(), 2);
    MacroSet reloaded;
    EXPECT_TRUE(reloaded.fromVar(pasted.toVar()));
}

TEST(SnippetMacro, MacroShrunkToOneMemberStillPastesAsAMacro) {
    juce::AudioProcessorGraph graph;
    auto osc = addAtWithUuid(graph, std::make_unique<OscillatorModule>(), 0, 0);

    MacroSet macros;
    Macro macro;
    macro.name = "Solo Member";
    macro.members = {osc->properties["uuid"].toString()};
    macros.add(macro);

    auto snippet = SnippetManager::extractSnippet(graph, {osc->nodeID}, "One", /*includeExtraState=*/false, macros);
    const auto pasted = insertIntoMacroSet(snippet, 1);
    ASSERT_EQ(pasted.size(), 1);
    EXPECT_EQ(pasted.getAll()[0].name, "Solo Member");
}
