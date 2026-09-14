// GraphEditor per-node action tests: module title rename (custom card titles), double-click port
// disconnect (issue #216), output-card identity treatment (module chrome), and Locate Master (FRO45)
// — actions and identity checks that target one existing node/card rather than graph wiring.
// Shared GraphEditorTest fixture and helpers live in GraphEditorTestHelpers.h.

#include "GraphEditorTestHelpers.h"

#include "AI/AIStateMapper/AIStateMapper.h"
#include "AppUndoManager.h"
#include "Mixer/MasterSplice.h"
#include "Modules/FX/ChorusModule.h"
#include "Modules/FX/DelayModule.h"
#include "Modules/MasterModule.h"
#include "Modules/ModuleBase.h"
#include "Modules/OscillatorModule.h"
#include "Modules/VCAModule.h"

// --- Double-click module title to rename (custom card titles) ----------------
//
// The custom title is the node property "displayName". It is deliberately NOT the processor's own
// name: ModuleBase::getName() is the auto-numbered "Chorus 2" that AudioEngine::updateModuleNames()
// recomputes wholesale on every graph change, so a title written there is clobbered by the next node
// added. These tests pin both halves — the custom title sticks, and the numbering still works.

TEST_F(GraphEditorTest, ModuleTitleRenameCommitsAndFallsBackToTheNumberedName) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(900, 600);

    auto& graph = engine.getGraph();
    auto node = graph.addNode(std::make_unique<ChorusModule>());
    engine.updateModuleNames(); // assigns "Chorus 1"
    editor.updateComponents();
    sizeModuleComponents(editor);

    auto* comp = findModuleComp(editor, node->getProcessor());
    ASSERT_NE(comp, nullptr);
    const auto numbered = node->getProcessor()->getName();
    ASSERT_TRUE(numbered.isNotEmpty());
    EXPECT_EQ(comp->cardTitle(), numbered) << "with no custom title the card shows the numbered name";

    comp->beginTitleRename();
    ASSERT_TRUE(comp->isRenamingTitle());
    auto* ed = dynamic_cast<juce::TextEditor*>(comp->findChildWithID("moduleTitleRenameEditor"));
    ASSERT_NE(ed, nullptr);
    EXPECT_EQ(ed->getText(), numbered) << "the editor is seeded with what the header shows";

    ed->setText("Shimmer Bus", juce::dontSendNotification);
    comp->finishTitleRename(true);

    EXPECT_FALSE(comp->isRenamingTitle());
    EXPECT_EQ(editor.getModuleDisplayName(node->nodeID), "Shimmer Bus");
    EXPECT_EQ(comp->cardTitle(), "Shimmer Bus");
    EXPECT_EQ(node->getProcessor()->getName(), numbered) << "the processor's numbered name is untouched";
}

TEST_F(GraphEditorTest, ModuleTitleRenameEscapeCancelsAndWhitespaceReverts) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(900, 600);

    auto& graph = engine.getGraph();
    auto node = graph.addNode(std::make_unique<ChorusModule>());
    engine.updateModuleNames();
    editor.updateComponents();
    sizeModuleComponents(editor);
    auto* comp = findModuleComp(editor, node->getProcessor());
    ASSERT_NE(comp, nullptr);
    const auto numbered = node->getProcessor()->getName();

    // Escape discards.
    comp->beginTitleRename();
    auto* ed = dynamic_cast<juce::TextEditor*>(comp->findChildWithID("moduleTitleRenameEditor"));
    ASSERT_NE(ed, nullptr);
    ed->setText("Discard Me", juce::dontSendNotification);
    comp->finishTitleRename(false);
    EXPECT_EQ(editor.getModuleDisplayName(node->nodeID), "") << "Escape must not store anything";
    EXPECT_EQ(comp->cardTitle(), numbered);

    // Commit a real name, then blank it out: whitespace reverts to the numbered default.
    editor.setModuleDisplayName(node->nodeID, "Shimmer Bus");
    ASSERT_EQ(comp->cardTitle(), "Shimmer Bus");

    comp->beginTitleRename();
    ed = dynamic_cast<juce::TextEditor*>(comp->findChildWithID("moduleTitleRenameEditor"));
    ASSERT_NE(ed, nullptr);
    ed->setText("    ", juce::dontSendNotification);
    comp->finishTitleRename(true);
    EXPECT_EQ(editor.getModuleDisplayName(node->nodeID), "") << "whitespace-only clears the custom title";
    EXPECT_EQ(comp->cardTitle(), numbered);
}

TEST_F(GraphEditorTest, ModuleTitleRenameIsUndoable) {
    AudioEngine engine;
    AppUndoManager undoMgr;
    GraphEditor editor(engine, &undoMgr);
    editor.setSize(900, 600);

    auto& graph = engine.getGraph();
    auto node = graph.addNode(std::make_unique<ChorusModule>());
    engine.updateModuleNames();
    editor.updateComponents();
    sizeModuleComponents(editor);
    const auto nodeId = node->nodeID;

    editor.setModuleDisplayName(nodeId, "First Name");
    ASSERT_EQ(editor.getModuleDisplayName(nodeId), "First Name");
    editor.setModuleDisplayName(nodeId, "Second Name");
    ASSERT_EQ(editor.getModuleDisplayName(nodeId), "Second Name");

    ASSERT_TRUE(undoMgr.undo());
    const auto afterUndo = findNodeIdByName(graph, "Chorus 1");
    // The node id survives a structural restore here, but resolve by whatever is live to be safe.
    const auto liveId = afterUndo.uid != 0 ? afterUndo : nodeId;
    EXPECT_EQ(editor.getModuleDisplayName(liveId), "First Name") << "undo restores the previous title";
}

TEST_F(GraphEditorTest, ModuleTitleRenameDoesNotArmABodyDrag) {
    // The double-click must be intercepted BEFORE the drag is armed, or bodyDragActive stays set
    // under an open editor and the next stray mouseDrag walks the card off under the cursor.
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(900, 600);

    auto& graph = engine.getGraph();
    auto node = graph.addNode(std::make_unique<ChorusModule>());
    engine.updateModuleNames();
    editor.updateComponents();
    sizeModuleComponents(editor);
    auto* comp = findModuleComp(editor, node->getProcessor());
    ASSERT_NE(comp, nullptr);

    const auto before = comp->getPosition();
    const juce::Point<int> titlePoint{comp->getWidth() / 2, 8}; // inside the header band
    comp->mouseDown(makeModuleClickWithMods(*comp, titlePoint, plainLeftClick(), /*clicks=*/2));

    ASSERT_TRUE(comp->isRenamingTitle()) << "a double-click on the header opens the editor";

    // A drag arriving now must not move the card: no dragger was armed.
    comp->mouseDrag(makeModuleClickWithMods(*comp, titlePoint + juce::Point<int>(80, 40), plainLeftClick()));
    EXPECT_EQ(comp->getPosition(), before) << "the rename must not have armed a body drag";

    comp->finishTitleRename(false);
}

TEST_F(GraphEditorTest, ModuleTitleRoundTripsThroughGraphJSON) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(900, 600);

    auto& graph = engine.getGraph();
    auto node = graph.addNode(std::make_unique<ChorusModule>());
    engine.updateModuleNames();
    editor.updateComponents();
    editor.setModuleDisplayName(node->nodeID, "Shimmer Bus");

    const auto json = synth::AIStateMapper::graphToJSON(graph);
    const auto text = juce::JSON::toString(json);
    EXPECT_TRUE(text.contains("Shimmer Bus")) << "the custom title must be serialized";

    // Trusted reload: the title comes back.
    AudioEngine reloaded;
    ASSERT_TRUE(synth::AIStateMapper::applyJSONToGraph(json, reloaded.getGraph(), /*clearExisting=*/true,
                                                       /*trusted=*/true));
    bool found = false;
    for (auto* n : reloaded.getGraph().getNodes()) {
        if (n->getProcessor() != nullptr && n->getProcessor()->getName().startsWith("Chorus")) {
            EXPECT_EQ(n->properties["displayName"].toString(), "Shimmer Bus");
            found = true;
        }
    }
    EXPECT_TRUE(found) << "the renamed Chorus survived the round trip";
}

TEST_F(GraphEditorTest, UntrustedPatchDisplayNameIsCappedAndDisplayOnly) {
    // Display-only and length-capped on the untrusted path: it may relabel a card and nothing else.
    // In particular it must never influence which module type gets created — that is "type".
    AudioEngine engine;
    const juce::String oversized = juce::String::repeatedString("A", synth::kMaxModuleDisplayNameChars * 4);

    juce::String patch = R"({"nodes":[{"id":1,"type":"Chorus","displayName":")" + oversized + R"("}]})";
    const auto json = juce::JSON::parse(patch);
    ASSERT_TRUE(synth::AIStateMapper::applyJSONToGraph(json, engine.getGraph(), /*clearExisting=*/true,
                                                       /*trusted=*/false));

    int chorusCount = 0;
    for (auto* n : engine.getGraph().getNodes()) {
        if (n->getProcessor() == nullptr)
            continue;
        if (dynamic_cast<ChorusModule*>(n->getProcessor()) != nullptr) {
            ++chorusCount;
            const auto stored = n->properties["displayName"].toString();
            EXPECT_LE(stored.length(), synth::kMaxModuleDisplayNameChars) << "an untrusted title must be capped";
            EXPECT_TRUE(stored.isNotEmpty());
        }
    }
    EXPECT_EQ(chorusCount, 1) << "the title must not have changed which module type was created";
}

TEST_F(GraphEditorTest, AutoNumberingStillAppliesAlongsideCustomTitles) {
    // Renaming one instance must not disturb the numbering of the others, and a renamed card keeps
    // its own title while updateModuleNames renumbers the processors underneath.
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1200, 800);

    auto& graph = engine.getGraph();
    auto first = graph.addNode(std::make_unique<ChorusModule>());
    auto second = graph.addNode(std::make_unique<ChorusModule>());
    engine.updateModuleNames();
    editor.updateComponents();

    EXPECT_EQ(first->getProcessor()->getName(), "Chorus 1");
    EXPECT_EQ(second->getProcessor()->getName(), "Chorus 2");

    editor.setModuleDisplayName(first->nodeID, "Shimmer Bus");

    // A third instance arrives and everything renumbers.
    auto third = graph.addNode(std::make_unique<ChorusModule>());
    engine.updateModuleNames();
    editor.updateComponents();

    EXPECT_EQ(third->getProcessor()->getName(), "Chorus 3") << "auto-numbering still counts every instance";
    EXPECT_EQ(editor.getModuleDisplayName(first->nodeID), "Shimmer Bus")
        << "renumbering must not clobber a custom title";
    EXPECT_EQ(editor.getModuleTitle(first->nodeID, first->getProcessor()), "Shimmer Bus");
    EXPECT_EQ(editor.getModuleTitle(second->nodeID, second->getProcessor()), "Chorus 2")
        << "an un-renamed sibling still follows the numbering";
}

TEST_F(GraphEditorTest, ModuleTitleRenameIsDismissedByAPressOutsideTheEditor) {
    // The editor's own onFocusLost is not enough: almost nothing on this canvas wants keyboard
    // focus, so clicking a card body or the background never takes focus off the editor and the
    // callback never fires. Every press path has to commit it from the presser's side. All three
    // surfaces the user can plausibly click are covered here.
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1200, 800);

    auto& graph = engine.getGraph();
    auto a = graph.addNode(std::make_unique<ChorusModule>());
    a->properties.set("x", 40);
    a->properties.set("y", 40);
    auto b = graph.addNode(std::make_unique<DelayModule>());
    b->properties.set("x", 500);
    b->properties.set("y", 40);
    engine.updateModuleNames();
    editor.updateComponents();
    sizeModuleComponents(editor);

    auto* compA = findModuleComp(editor, a->getProcessor());
    auto* compB = findModuleComp(editor, b->getProcessor());
    ASSERT_NE(compA, nullptr);
    ASSERT_NE(compB, nullptr);

    const juce::Point<int> bodyA{compA->getWidth() / 2, compA->getHeight() / 2};
    const juce::Point<int> bodyB{compB->getWidth() / 2, compB->getHeight() / 2};

    auto openEditorWith = [&](const juce::String& text) {
        compA->beginTitleRename();
        auto* ed = dynamic_cast<juce::TextEditor*>(compA->findChildWithID("moduleTitleRenameEditor"));
        ASSERT_NE(ed, nullptr);
        ed->setText(text, juce::dontSendNotification);
    };

    // 1. A press on the SAME card's body.
    openEditorWith("Named By Body Click");
    ASSERT_TRUE(compA->isRenamingTitle());
    compA->mouseDown(makeModuleClickWithMods(*compA, bodyA, plainLeftClick()));
    EXPECT_FALSE(compA->isRenamingTitle()) << "a press on the card body must dismiss the editor";
    EXPECT_EQ(editor.getModuleDisplayName(a->nodeID), "Named By Body Click") << "clicking away commits";

    // 2. A press on ANOTHER card.
    openEditorWith("Named By Other Card");
    ASSERT_TRUE(compA->isRenamingTitle());
    compB->mouseDown(makeModuleClickWithMods(*compB, bodyB, plainLeftClick()));
    EXPECT_FALSE(compA->isRenamingTitle()) << "a press on a different card must dismiss it too";
    EXPECT_EQ(editor.getModuleDisplayName(a->nodeID), "Named By Other Card");

    // 3. A press on empty canvas.
    openEditorWith("Named By Canvas Click");
    ASSERT_TRUE(compA->isRenamingTitle());
    editor.mouseDown(makeModuleClickWithMods(editor, {1000, 700}, plainLeftClick()));
    EXPECT_FALSE(compA->isRenamingTitle()) << "a press on empty canvas must dismiss it";
    EXPECT_EQ(editor.getModuleDisplayName(a->nodeID), "Named By Canvas Click");

    // Escape is still the only cancel, and it must survive all of the above wiring.
    openEditorWith("Should Not Stick");
    compA->finishTitleRename(false);
    EXPECT_EQ(editor.getModuleDisplayName(a->nodeID), "Named By Canvas Click") << "Escape still discards";
}

// --- Double-click port disconnect (issue #216) -------------------------------

static juce::MouseEvent makeModuleClick(juce::Component& comp, juce::Point<int> position, int clicks) {
    const auto pos = position.toFloat();
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), pos, juce::ModifierKeys(), 0.0f, 0.0f,
                            0.0f, 0.0f, 0.0f, &comp, &comp, juce::Time::getCurrentTime(), pos,
                            juce::Time::getCurrentTime(), clicks, false);
}

TEST_F(GraphEditorTest, DoubleClickConnectedPortDisconnectsByDefault) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    auto oscNode = engine.getGraph().addNode(std::make_unique<OscillatorModule>());
    auto vcaNode = engine.getGraph().addNode(std::make_unique<VCAModule>());
    editor.updateComponents();
    sizeModuleComponents(editor);

    auto* oscComp = findModuleComp(editor, oscNode->getProcessor());
    auto* vcaComp = findModuleComp(editor, vcaNode->getProcessor());
    ASSERT_NE(oscComp, nullptr);
    ASSERT_NE(vcaComp, nullptr);

    editor.connectPorts(oscNode->nodeID, 0, vcaNode->nodeID, 0, false, false);
    ASSERT_EQ(countAudioConnectionsBetween(engine.getGraph(), oscNode->nodeID, vcaNode->nodeID), 1);
    ASSERT_TRUE(editor.isPortConnected(oscComp, 0, false, false));
    ASSERT_TRUE(editor.getDoubleClickPortDisconnectEnabled());

    oscComp->mouseDown(makeModuleClick(*oscComp, oscComp->getPortCenter(0, false), 2));

    EXPECT_EQ(countAudioConnectionsBetween(engine.getGraph(), oscNode->nodeID, vcaNode->nodeID), 0);
    EXPECT_FALSE(editor.isPortConnected(oscComp, 0, false, false));
    EXPECT_FALSE(editor.isPortConnected(vcaComp, 0, true, false));
}

TEST_F(GraphEditorTest, DoubleClickDoesNotDisconnectWhenPreferenceOff) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);
    editor.setDoubleClickPortDisconnectEnabled(false);

    auto oscNode = engine.getGraph().addNode(std::make_unique<OscillatorModule>());
    auto vcaNode = engine.getGraph().addNode(std::make_unique<VCAModule>());
    editor.updateComponents();
    sizeModuleComponents(editor);

    auto* oscComp = findModuleComp(editor, oscNode->getProcessor());
    ASSERT_NE(oscComp, nullptr);

    editor.connectPorts(oscNode->nodeID, 0, vcaNode->nodeID, 0, false, false);
    ASSERT_EQ(countAudioConnectionsBetween(engine.getGraph(), oscNode->nodeID, vcaNode->nodeID), 1);

    oscComp->mouseDown(makeModuleClick(*oscComp, oscComp->getPortCenter(0, false), 2));

    EXPECT_EQ(countAudioConnectionsBetween(engine.getGraph(), oscNode->nodeID, vcaNode->nodeID), 1);
}

TEST_F(GraphEditorTest, DoubleClickUnconnectedPortIsANoOp) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    auto oscNode = engine.getGraph().addNode(std::make_unique<OscillatorModule>());
    editor.updateComponents();
    sizeModuleComponents(editor);

    auto* oscComp = findModuleComp(editor, oscNode->getProcessor());
    ASSERT_NE(oscComp, nullptr);
    ASSERT_FALSE(editor.isPortConnected(oscComp, 0, false, false));

    oscComp->mouseDown(makeModuleClick(*oscComp, oscComp->getPortCenter(0, false), 2));

    EXPECT_EQ(engine.getGraph().getConnections().size(), 0u);
    EXPECT_FALSE(editor.isPortConnected(oscComp, 0, false, false));
}

// --- Output-card identity treatment (module chrome) --------------------------
// GraphEditor::setOutputDeviceInfoProvider / refreshOutputDeviceInfo: MainComponent -> GraphEditor
// -> the Audio Output ModuleComponent, refreshed only when told to (no polling — see
// ModuleComponent::setOutputDeviceInfoText and docs/layout.md's module chrome section).

TEST_F(GraphEditorTest, RefreshOutputDeviceInfoPushesProviderTextToTheOutputCard) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    auto outNode = addAudioOutputNode(engine.getGraph(), 400, 100);
    editor.updateComponents();
    sizeModuleComponents(editor);

    auto* outComp = findModuleComp(editor, outNode->getProcessor());
    ASSERT_NE(outComp, nullptr);
    EXPECT_TRUE(outComp->getOutputDeviceInfoTextForTest().isEmpty()) << "nothing pushed in yet";

    editor.setOutputDeviceInfoProvider([] { return juce::String("Test Device - 48 kHz - 2ch"); });
    editor.refreshOutputDeviceInfo();

    EXPECT_EQ(outComp->getOutputDeviceInfoTextForTest(), juce::String("Test Device - 48 kHz - 2ch"));
}

TEST_F(GraphEditorTest, RefreshOutputDeviceInfoLeavesOtherCardsUntouched) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    auto outNode = addAudioOutputNode(engine.getGraph(), 400, 100);
    auto oscNode = engine.getGraph().addNode(std::make_unique<OscillatorModule>());
    editor.updateComponents();
    sizeModuleComponents(editor);

    auto* oscComp = findModuleComp(editor, oscNode->getProcessor());
    ASSERT_NE(oscComp, nullptr);
    juce::ignoreUnused(outNode);

    editor.setOutputDeviceInfoProvider([] { return juce::String("Test Device - 48 kHz - 2ch"); });
    editor.refreshOutputDeviceInfo();

    EXPECT_TRUE(oscComp->getOutputDeviceInfoTextForTest().isEmpty())
        << "the identity treatment (and the text it carries) is Audio-Output-only";
}

TEST_F(GraphEditorTest, RefreshOutputDeviceInfoWithNoProviderIsANoOp) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    auto outNode = addAudioOutputNode(engine.getGraph(), 400, 100);
    editor.updateComponents();
    sizeModuleComponents(editor);

    auto* outComp = findModuleComp(editor, outNode->getProcessor());
    ASSERT_NE(outComp, nullptr);

    // No provider installed (e.g. before MainComponent wires one up) — must not crash.
    EXPECT_NO_THROW(editor.refreshOutputDeviceInfo());
    EXPECT_TRUE(outComp->getOutputDeviceInfoTextForTest().isEmpty());
}

// Hosted mode (or a device that just closed) degrades to an empty provider result, which must
// clear a previously-shown line rather than leaving it stale.
TEST_F(GraphEditorTest, RefreshOutputDeviceInfoClearsTextWhenProviderReturnsEmpty) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    auto outNode = addAudioOutputNode(engine.getGraph(), 400, 100);
    editor.updateComponents();
    sizeModuleComponents(editor);

    auto* outComp = findModuleComp(editor, outNode->getProcessor());
    ASSERT_NE(outComp, nullptr);

    editor.setOutputDeviceInfoProvider([] { return juce::String("Test Device - 48 kHz - 2ch"); });
    editor.refreshOutputDeviceInfo();
    ASSERT_FALSE(outComp->getOutputDeviceInfoTextForTest().isEmpty());

    editor.setOutputDeviceInfoProvider([] { return juce::String(); }); // e.g. HostMode::Hosted
    editor.refreshOutputDeviceInfo();
    EXPECT_TRUE(outComp->getOutputDeviceInfoTextForTest().isEmpty());
}

// --- Locate Master (FRO45) ----------------------------------------------------------------------

static juce::AudioProcessorGraph::Node* findAudioOutputNodeInGraph(juce::AudioProcessorGraph& graph) {
    using IOProcessor = juce::AudioProcessorGraph::AudioGraphIOProcessor;
    for (auto* node : graph.getNodes()) {
        if (node == nullptr || node->getProcessor() == nullptr)
            continue;
        if (auto* io = dynamic_cast<IOProcessor*>(node->getProcessor()))
            if (io->getType() == IOProcessor::audioOutputNode)
                return node;
    }
    return nullptr;
}

// Founder feedback on T183's live check: auto-arrange or a drag can leave Master anywhere on the
// canvas. GraphEditor::locateMasterOrOutput() selects it and pans the view so it's on screen.
TEST_F(GraphEditorTest, LocateMasterSelectsMasterAndPansItIntoView) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1000, 800);

    auto& graph = engine.getGraph();
    auto masterNode = graph.addNode(std::make_unique<MasterModule>());
    // Far outside the [0,1000]x[0,800] viewport centreViewOn/getVisibleCanvasRect start with.
    masterNode->properties.set("x", 8000);
    masterNode->properties.set("y", 8000);
    editor.updateComponents();
    sizeModuleComponents(editor);

    ASSERT_TRUE(editor.hasLocatableMasterOrOutput());
    ASSERT_FALSE(editor.getVisibleCanvasRect().contains(juce::Point<float>(8000.0f, 8000.0f)))
        << "precondition: Master starts off-screen";

    EXPECT_EQ(editor.locateMasterOrOutput(), GraphEditor::LocateMasterResult::Master);

    const auto selected = editor.getSelectedNodes();
    ASSERT_EQ(selected.size(), 1u);
    EXPECT_EQ(selected[0], masterNode->nodeID);

    auto* comp = findModuleComp(editor, masterNode->getProcessor());
    ASSERT_NE(comp, nullptr);
    EXPECT_TRUE(editor.getVisibleCanvasRect().contains(comp->getBounds().toFloat().getCentre()))
        << "the viewport must now contain Master";
}

// No Master yet (the common case before the first channel exists, docs/mixer.md) — falls back to
// Audio Output, which T187 seeds on New Patch (a bare test AudioEngine starts with NEITHER node
// until something adds one — see addAudioOutputNode above — so this seeds one explicitly).
TEST_F(GraphEditorTest, LocateMasterFallsBackToAudioOutputWhenThereIsNoMaster) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1000, 800);

    auto& graph = engine.getGraph();
    auto outputNode = addAudioOutputNode(graph, 9000, 9000);
    editor.updateComponents();
    sizeModuleComponents(editor);

    ASSERT_EQ(synth::findMasterNode(graph), nullptr) << "precondition: no Master yet";
    ASSERT_TRUE(editor.hasLocatableMasterOrOutput());

    EXPECT_EQ(editor.locateMasterOrOutput(), GraphEditor::LocateMasterResult::AudioOutput);

    const auto selected = editor.getSelectedNodes();
    ASSERT_EQ(selected.size(), 1u);
    EXPECT_EQ(selected[0], outputNode->nodeID);

    auto* comp = findModuleComp(editor, outputNode->getProcessor());
    ASSERT_NE(comp, nullptr);
    EXPECT_TRUE(editor.getVisibleCanvasRect().contains(comp->getBounds().toFloat().getCentre()));
}

// Neither node exists — a bare test AudioEngine starts empty (only MainComponent's real
// createDefaultPatch() seeds an Audio Output), so this is the natural starting state, not
// something to engineer. Graceful no-op, and the predicate the context menu item / command's
// setActive() both read must agree there's nothing to find.
TEST_F(GraphEditorTest, LocateMasterIsANoOpAndReportsInactiveWithNeitherNode) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1000, 800);

    auto& graph = engine.getGraph();
    ASSERT_EQ(synth::findMasterNode(graph), nullptr);
    ASSERT_EQ(findAudioOutputNodeInGraph(graph), nullptr) << "precondition: a bare test engine has no nodes at all";
    EXPECT_FALSE(editor.hasLocatableMasterOrOutput());

    const auto viewportBefore = editor.getVisibleCanvasRect();
    EXPECT_EQ(editor.locateMasterOrOutput(), GraphEditor::LocateMasterResult::NoTarget);
    EXPECT_TRUE(editor.getSelectedNodes().empty()) << "graceful no-op: nothing gets selected";
    EXPECT_EQ(editor.getVisibleCanvasRect(), viewportBefore) << "graceful no-op: the view must not move";
}
