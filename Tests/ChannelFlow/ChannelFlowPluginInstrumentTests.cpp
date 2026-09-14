// ChannelFlowPluginInstrumentTests.cpp
//
// FRO42 (P9-3h): "+ Track -> Instrument -> Plugin -> <name>" — a hosted plugin as the
// instrument, plus the FRO42 review-fix regressions (menu snapshot resolution, format-label
// disambiguation, self-exclusion, poly Voice Mixer wiring). Uses InstrumentPluginStubBackendCFT
// and friends from ChannelFlowTestFixture.h.

#include "../../Source/AI/AIProvider.h"
#include "../../Source/AI/AIStateMapper/AIStateMapper.h"
#include "../../Source/AudioEngine.h"
#include "../../Source/Branding.h"
#include "../../Source/MacroSet.h"
#include "../../Source/Mixer/ChannelFlows.h"
#include "../../Source/Mixer/MasterSplice.h"
#include "../../Source/Modules/ChannelStripModule.h"
#include "../../Source/Modules/MasterModule.h"
#include "../../Source/Modules/ModuleBase.h"
#include "../../Source/Modules/VCAModule.h"
#include "../../Source/Plugin/Hosting/HostedPluginModule.h"
#include "../../Source/Plugin/Hosting/PluginScanService.h"
#include "../../Source/Timeline/TimelineDoc.h"
#include "../StubPluginInstance.h"
#include "ChannelFlowTestFixture.h"
#include "MainComponent/MainComponent.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Library/ModuleLibraryComponent/ModuleLibraryComponent.h"
#include "UI/Timeline/TimelineTrackHeaderComponent.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <gtest/gtest.h>
#include <map>
#include <memory>
#include <thread>

// ============================================================================
// FRO42 (P9-3h): "+ Track -> Instrument -> Plugin -> <name>" — a hosted plugin as the instrument.
// Loading is asynchronous (StubBackend's own contract), so every test here pumps the message loop
// after driving the SAME applyAddTrackMenuChoice/menu-id path the tests above use.
// ============================================================================

TEST_F(ChannelFlowTest, PluginInstrumentTrackBuildsDefaultChannelWithNoAdsr) {
    InstrumentPluginStubBackendCFT backend;
    backend.factories["Stub Synth"] = [] {
        return std::make_unique<synth::test::StubPluginInstance>(0, 2, "Stub Synth");
    };
    synth::HostedPluginBackend::ScopedDefault installed(&backend);

    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    auto& graph = mc.getAudioEngine().getGraph();
    auto& macros = mc.getGraphEditor().getMacros();
    mc.getPluginScanService().setCandidateSource([](const juce::String&) { return juce::StringArray(); });
    seedScanListCFT(mc.getPluginScanService(), {pluginDescriptionCFT("Stub Synth", 0xA1FA, /*isInstrument=*/true)});

    // FRO42 fix: applyAddTrackMenuChoice now resolves against the snapshot buildAddTrackMenu()
    // captures, so a real menu open has to run first — the same flow a real click always goes
    // through (see TimelinePanelComponent.h's instrumentPluginMenuSnapshot_ comment).
    mc.getTimelinePanel().buildAddTrackMenu();
    mc.getTimelinePanel().applyAddTrackMenuChoice(synth::ui::TimelinePanelComponent::kAddInstrumentPluginMenuIdBase +
                                                  0);

    ASSERT_TRUE(pumpUntilCFT([&] { return countNodesOfTypeCFT(graph, ModuleType::ChannelStrip) == 1; }))
        << "the async load/chain-build never completed";

    auto* trackIn = findNodeOfTypeCFT(graph, ModuleType::TimelineMidiSource);
    auto* plugin = findNodeOfTypeCFT(graph, ModuleType::HostedPlugin);
    auto* eq = findNodeOfTypeCFT(graph, ModuleType::ParametricEQ);
    auto* comp = findNodeOfTypeCFT(graph, ModuleType::Compressor);
    auto* strip = findNodeOfTypeCFT(graph, ModuleType::ChannelStrip);
    ASSERT_NE(trackIn, nullptr);
    ASSERT_NE(plugin, nullptr);
    ASSERT_NE(eq, nullptr);
    ASSERT_NE(comp, nullptr);
    ASSERT_NE(strip, nullptr);
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::Master), 1);

    ASSERT_EQ(macros.size(), 1);
    const auto& macro = macros.getAll().front();
    EXPECT_EQ(findMacroMemberOfTypeCFT(graph, macro, ModuleType::ADSR), nullptr)
        << "a hosted synth has its own envelope — no P9-3i ADSR+VCA for it";
    EXPECT_EQ(findMacroMemberOfTypeCFT(graph, macro, ModuleType::VCA), nullptr);
    std::vector<juce::String> expectedMembers{nodeUuid(trackIn), nodeUuid(plugin), nodeUuid(eq), nodeUuid(comp),
                                              nodeUuid(strip)};
    auto actualMembers = macro.members;
    std::sort(expectedMembers.begin(), expectedMembers.end());
    std::sort(actualMembers.begin(), actualMembers.end());
    EXPECT_EQ(actualMembers, expectedMembers) << "exactly Track In/plugin/EQ/Compressor/Strip, nothing else";

    bool midiWired = false;
    for (const auto& c : graph.getConnections())
        if (c.source.nodeID == trackIn->nodeID &&
            c.source.channelIndex == juce::AudioProcessorGraph::midiChannelIndex &&
            c.destination.nodeID == plugin->nodeID &&
            c.destination.channelIndex == juce::AudioProcessorGraph::midiChannelIndex)
            midiWired = true;
    EXPECT_TRUE(midiWired) << "Track In's MIDI must reach the plugin's MIDI input";

    bool leftWired = false;
    bool rightWired = false;
    for (const auto& c : graph.getConnections()) {
        if (c.source.nodeID != plugin->nodeID || c.destination.nodeID != eq->nodeID)
            continue;
        if (c.source.channelIndex == 0 && c.destination.channelIndex == 0)
            leftWired = true;
        if (c.source.channelIndex == 1 && c.destination.channelIndex == 1)
            rightWired = true;
    }
    EXPECT_TRUE(leftWired) << "the plugin's L output must reach EQ L";
    EXPECT_TRUE(rightWired) << "the plugin's real published R output (raw ch1) must reach EQ R — "
                               "rightAudioLegChannel() read AFTER the load completed, never assumed ch1 blind";
}

TEST_F(ChannelFlowTest, PluginInstrumentTrackOneUndoStepRevertsEverythingAndRedoRestores) {
    InstrumentPluginStubBackendCFT backend;
    backend.factories["Stub Synth"] = [] {
        return std::make_unique<synth::test::StubPluginInstance>(0, 2, "Stub Synth");
    };
    synth::HostedPluginBackend::ScopedDefault installed(&backend);

    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    auto& graph = mc.getAudioEngine().getGraph();
    auto& doc = mc.getTimelineDoc();
    auto& macros = mc.getGraphEditor().getMacros();
    mc.getPluginScanService().setCandidateSource([](const juce::String&) { return juce::StringArray(); });
    seedScanListCFT(mc.getPluginScanService(), {pluginDescriptionCFT("Stub Synth", 0xA1FA, /*isInstrument=*/true)});

    // Settle first: GraphEditor::updateComponents() (already run once, for the factory preset the
    // constructor loads) posts a mod-matrix refresh via callAsync, and that refresh renumbers every
    // module's display name THE FIRST TIME IT EVER RUNS (AudioEngine::updateModuleNames() — cosmetic,
    // never undo-tracked). The synchronous factory-instrument flow never pumps the loop, so that
    // rename never gets a chance to fire there; THIS flow necessarily pumps it for the async plugin
    // load below, so it must be allowed to happen and settle BEFORE "before" is captured, or it would
    // land in the gap between "before" and "after undo" and make them differ over nothing this
    // feature touched.
    pumpUntilCFT([] { return false; }, 50);

    const juce::String graphBefore = juce::JSON::toString(synth::AIStateMapper::graphToJSON(graph));
    const juce::String docBefore = juce::JSON::toString(doc.toVar());
    const juce::String macrosBefore = juce::JSON::toString(macros.toVar());

    // FRO42 fix: resolve against the buildAddTrackMenu() snapshot, the real click flow.
    mc.getTimelinePanel().buildAddTrackMenu();
    mc.getTimelinePanel().applyAddTrackMenuChoice(synth::ui::TimelinePanelComponent::kAddInstrumentPluginMenuIdBase +
                                                  0);
    ASSERT_TRUE(pumpUntilCFT([&] { return countNodesOfTypeCFT(graph, ModuleType::ChannelStrip) == 1; }));

    const juce::String graphAfter = juce::JSON::toString(synth::AIStateMapper::graphToJSON(graph));
    const juce::String docAfter = juce::JSON::toString(doc.toVar());
    const juce::String macrosAfter = juce::JSON::toString(macros.toVar());

    ASSERT_TRUE(mc.getUndoManager().canUndo());
    ASSERT_TRUE(mc.getUndoManager().undo());
    EXPECT_EQ(juce::JSON::toString(synth::AIStateMapper::graphToJSON(graph)), graphBefore);
    EXPECT_EQ(juce::JSON::toString(doc.toVar()), docBefore);
    EXPECT_EQ(juce::JSON::toString(macros.toVar()), macrosBefore);
    EXPECT_FALSE(mc.getUndoManager().canUndo()) << "the whole channel was ONE undo step";

    ASSERT_TRUE(mc.getUndoManager().redo());
    EXPECT_EQ(juce::JSON::toString(synth::AIStateMapper::graphToJSON(graph)), graphAfter);
    EXPECT_EQ(juce::JSON::toString(doc.toVar()), docAfter);
    EXPECT_EQ(juce::JSON::toString(macros.toVar()), macrosAfter);
}

TEST_F(ChannelFlowTest, PluginInstrumentTrackEffectsAreNeverOfferedInTheMenu) {
    InstrumentPluginStubBackendCFT backend;
    synth::HostedPluginBackend::ScopedDefault installed(&backend);

    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    auto& graph = mc.getAudioEngine().getGraph();
    auto& doc = mc.getTimelineDoc();
    mc.getPluginScanService().setCandidateSource([](const juce::String&) { return juce::StringArray(); });
    seedScanListCFT(mc.getPluginScanService(), {pluginDescriptionCFT("Stub Delay", 0xB37A, /*isInstrument=*/false)});

    EXPECT_TRUE(mc.getTimelinePanel().collectInstrumentPluginMenuOptions().empty())
        << "an effect (isInstrument=false) must never appear in the Instrument -> Plugin submenu";

    // Build the real menu (as a real click would) so the disabled "No instrument plugins found"
    // row is what actually snapshots — not a panel that never opened its menu at all.
    mc.getTimelinePanel().buildAddTrackMenu();

    const int tracksBefore = (int)doc.getTracks().size();
    const int nodesBefore = graph.getNumNodes();
    // Nothing at this id — the snapshot is empty, so the index is out of range and this must be a
    // no-op, exactly like a stale/out-of-range id on any other dynamically-built menu in this app.
    mc.getTimelinePanel().applyAddTrackMenuChoice(synth::ui::TimelinePanelComponent::kAddInstrumentPluginMenuIdBase +
                                                  0);

    EXPECT_EQ((int)doc.getTracks().size(), tracksBefore);
    EXPECT_EQ(graph.getNumNodes(), nodesBefore);
    EXPECT_FALSE(mc.getUndoManager().canUndo());
}

TEST_F(ChannelFlowTest, PluginInstrumentTrackFailedLoadLeavesGraphAndUndoUntouched) {
    // "Broken Synth" is scanned (isInstrument=true, so it IS offered) but has no factory entry —
    // InstrumentPluginStubBackendCFT fails its load, exactly like a plugin whose binary the machine
    // can no longer find or whose format crashed on load.
    InstrumentPluginStubBackendCFT backend;
    synth::HostedPluginBackend::ScopedDefault installed(&backend);

    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    auto& graph = mc.getAudioEngine().getGraph();
    auto& doc = mc.getTimelineDoc();
    mc.getPluginScanService().setCandidateSource([](const juce::String&) { return juce::StringArray(); });
    seedScanListCFT(mc.getPluginScanService(), {pluginDescriptionCFT("Broken Synth", 0xC0DE, /*isInstrument=*/true)});

    const int tracksBefore = (int)doc.getTracks().size();
    const int nodesBefore = graph.getNumNodes();
    const juce::String messageBefore = mc.getStatusBar().getTransientMessageForTest();

    // FRO42 fix: resolve against the buildAddTrackMenu() snapshot, the real click flow.
    mc.getTimelinePanel().buildAddTrackMenu();
    mc.getTimelinePanel().applyAddTrackMenuChoice(synth::ui::TimelinePanelComponent::kAddInstrumentPluginMenuIdBase +
                                                  0);

    ASSERT_TRUE(pumpUntilCFT([&] { return mc.getStatusBar().getTransientMessageForTest() != messageBefore; }))
        << "the failure was never reported";

    EXPECT_EQ((int)doc.getTracks().size(), tracksBefore) << "a failed load must leave no orphan track";
    EXPECT_EQ(graph.getNumNodes(), nodesBefore) << "a failed load must leave no orphan node — not even a bare "
                                                   "Hosted Plugin placeholder";
    EXPECT_FALSE(mc.getUndoManager().canUndo()) << "a failed load must push no undo step";

    // The pending processor is torn down on a LATER message-loop turn (see
    // MainComponent::addInstrumentPluginTrack's own comment on why); give it that turn and confirm
    // nothing changed as a result either.
    pumpUntilCFT([] { return false; }, 100);
    EXPECT_EQ((int)doc.getTracks().size(), tracksBefore);
    EXPECT_EQ(graph.getNumNodes(), nodesBefore);
    EXPECT_FALSE(mc.getUndoManager().canUndo());
}

TEST_F(ChannelFlowTest, PluginInstrumentMenuShowsScanningThenNoInstrumentPluginsFound) {
    InstrumentPluginStubBackendCFT backend;
    synth::HostedPluginBackend::ScopedDefault installed(&backend);

    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();

    // One VST3 candidate with a launcher that deliberately takes a moment — long enough that
    // isScanning() is still reliably true the instant buildAddTrackMenu() returns (with zero
    // candidates the background thread can finish before this thread's very next line runs, which
    // would make that assertion flaky). It reports "not found" either way, so the scan still
    // finishes with nothing known — the "nothing installed on this machine" case.
    mc.getPluginScanService().setCandidateSource([](const juce::String& format) {
        return format == "VST3" ? juce::StringArray("/plugins/Slow.vst3") : juce::StringArray();
    });
    mc.getPluginScanService().setChildLauncher([](const juce::String&, const juce::String&, int, juce::String&) {
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        return false;
    });

    juce::PopupMenu firstOpen = mc.getTimelinePanel().buildAddTrackMenu();
    EXPECT_TRUE(mc.getPluginScanService().isScanning()) << "opening the menu must kick off the eager scan";
    const auto* scanningItem = findMenuItemByTextCFT(firstOpen, "Scanning for plugins...");
    ASSERT_NE(scanningItem, nullptr);
    EXPECT_FALSE(scanningItem->isEnabled);

    ASSERT_TRUE(pumpUntilCFT([&] { return !mc.getPluginScanService().isScanning(); }));

    juce::PopupMenu secondOpen = mc.getTimelinePanel().buildAddTrackMenu();
    const auto* noneItem = findMenuItemByTextCFT(secondOpen, "No instrument plugins found");
    ASSERT_NE(noneItem, nullptr);
    EXPECT_FALSE(noneItem->isEnabled);
}

// ============================================================================
// FRO42 review fixes (P9-3h follow-up). See TimelinePanelComponent.h's instrumentPluginMenuSnapshot_
// and kAddInstrumentPluginNoneMenuId comments, and MainComponent::getInstrumentPluginOptions'/
// addInstrumentPluginTrack's own comments, for the mechanism each of these proves.
// ============================================================================

TEST_F(ChannelFlowTest, PluginInstrumentMenuChoiceResolvesAgainstSnapshotNotALaterRescan) {
    // BLOCKER regression test. Before the fix, applyAddTrackMenuChoice re-ran
    // collectInstrumentPluginMenuOptions() at CLICK time and indexed it with the id baked in at
    // BUILD time — so anything that changed the known-plugin list between open and click (the live
    // repro: a background PluginScanService::runScan finishing) silently changed what index 0
    // meant, resolving the click against a plugin the menu never actually showed there. Reproduced
    // here without a real background-thread race: build the menu with ONE plugin known (so it lands
    // at index 0), capture that item's real id, THEN seed a second plugin that sorts alphabetically
    // BEFORE it — WITHOUT rebuilding the menu — and apply the ORIGINAL id. It must still resolve to
    // the plugin the menu actually showed at that id, never the one a fresh collect would now put
    // there.
    InstrumentPluginStubBackendCFT backend;
    backend.factories["Zebra Synth"] = [] {
        return std::make_unique<synth::test::StubPluginInstance>(0, 2, "Zebra Synth");
    };
    backend.factories["Aardvark Synth"] = [] {
        return std::make_unique<synth::test::StubPluginInstance>(0, 2, "Aardvark Synth");
    };
    synth::HostedPluginBackend::ScopedDefault installed(&backend);

    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    auto& graph = mc.getAudioEngine().getGraph();
    mc.getPluginScanService().setCandidateSource([](const juce::String&) { return juce::StringArray(); });
    seedScanListCFT(mc.getPluginScanService(), {pluginDescriptionCFT("Zebra Synth", 0xEEEE, /*isInstrument=*/true)});

    juce::PopupMenu menu = mc.getTimelinePanel().buildAddTrackMenu();
    const auto* zebraItem = findMenuItemByTextCFT(menu, "Zebra Synth (VST3)");
    ASSERT_NE(zebraItem, nullptr) << "the menu must show the one known instrument plugin, format-labelled";
    const int capturedId = zebraItem->itemID;

    // A second plugin sorting BEFORE "Zebra Synth" — a fresh collectInstrumentPluginMenuOptions()
    // would now put THIS at index 0. The menu is deliberately never rebuilt after this point.
    seedScanListCFT(mc.getPluginScanService(), {pluginDescriptionCFT("Zebra Synth", 0xEEEE, /*isInstrument=*/true),
                                                pluginDescriptionCFT("Aardvark Synth", 0xAAAA, /*isInstrument=*/true)});
    ASSERT_EQ(mc.getTimelinePanel().collectInstrumentPluginMenuOptions()[0].name, juce::String("Aardvark Synth"))
        << "sanity: a fresh collect now DOES put a different plugin at index 0";

    mc.getTimelinePanel().applyAddTrackMenuChoice(capturedId);

    ASSERT_TRUE(pumpUntilCFT([&] { return countNodesOfTypeCFT(graph, ModuleType::ChannelStrip) == 1; }))
        << "the async load/chain-build never completed";
    auto* plugin = findNodeOfTypeCFT(graph, ModuleType::HostedPlugin);
    ASSERT_NE(plugin, nullptr);
    auto* hosted = dynamic_cast<synth::HostedPluginModule*>(plugin->getProcessor());
    ASSERT_NE(hosted, nullptr);
    EXPECT_EQ(hosted->getPluginName(), juce::String("Zebra Synth"))
        << "the instance loaded must be the one shown at the captured id — never Aardvark Synth, "
           "which is what a re-collected, freshly re-sorted lookup would have resolved to";
}

TEST_F(ChannelFlowTest, PluginInstrumentMenuAppendsFormatLabelSoIdenticalNamesAreDistinguishable) {
    // SHOULD-FIX: a VST3 and an AU build of the same product must not show as two identical,
    // unlabelled rows — see shortPluginFormatLabel's own comment in TimelinePanelComponent.cpp.
    InstrumentPluginStubBackendCFT backend;
    synth::HostedPluginBackend::ScopedDefault installed(&backend);

    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    mc.getPluginScanService().setCandidateSource([](const juce::String&) { return juce::StringArray(); });
    seedScanListCFT(mc.getPluginScanService(),
                    {pluginDescriptionCFT("Massive", 0x1001, /*isInstrument=*/true, "VST3"),
                     pluginDescriptionCFT("Massive", 0x1002, /*isInstrument=*/true, "AudioUnit")});

    juce::PopupMenu menu = mc.getTimelinePanel().buildAddTrackMenu();
    EXPECT_NE(findMenuItemByTextCFT(menu, "Massive (VST3)"), nullptr) << "the VST3 build must be labelled";
    EXPECT_NE(findMenuItemByTextCFT(menu, "Massive (AU)"), nullptr)
        << "the AudioUnit build must be labelled with the SHORT form, matching the sidebar's own "
           "format disambiguation (ModuleLibraryComponent)";
    EXPECT_EQ(findMenuItemByTextCFT(menu, "Massive"), nullptr) << "the bare, unlabelled name must never appear";
}

TEST_F(ChannelFlowTest, PluginInstrumentMenuExcludesThisAppsOwnPluginBuild) {
    // DECISION (finding 3): never offer to host AgentSynth's own VST3/AU build as an instrument
    // inside itself. Matched against synth::branding's product identity (name AND manufacturer),
    // never a literal re-typed in the picker — see MainComponent::getInstrumentPluginOptions.
    InstrumentPluginStubBackendCFT backend;
    synth::HostedPluginBackend::ScopedDefault installed(&backend);

    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    mc.getPluginScanService().setCandidateSource([](const juce::String&) { return juce::StringArray(); });
    seedScanListCFT(mc.getPluginScanService(),
                    {pluginDescriptionCFT(synth::branding::kProductName, 0x2001, /*isInstrument=*/true, "VST3",
                                          synth::branding::kCompanyName),
                     pluginDescriptionCFT(synth::branding::kProductName, 0x2002, /*isInstrument=*/true, "AudioUnit",
                                          synth::branding::kCompanyName),
                     pluginDescriptionCFT("Stub Synth", 0x2003, /*isInstrument=*/true)});

    const auto options = mc.getTimelinePanel().collectInstrumentPluginMenuOptions();
    for (const auto& option : options)
        EXPECT_FALSE(option.name.equalsIgnoreCase(synth::branding::kProductName))
            << "this app's own build must never appear in its own Instrument -> Plugin picker";
    ASSERT_EQ(options.size(), 1u) << "exactly the one real third-party instrument must remain";
    EXPECT_EQ(options[0].name, juce::String("Stub Synth"));

    // The library sidebar goes through a DIFFERENT collector (getKnownPluginIdentities(), never
    // getInstrumentPluginOptions()) and is deliberately left unfiltered.
    const auto sidebarPlugins = mc.getPluginScanService().getKnownPluginIdentities();
    EXPECT_EQ(sidebarPlugins.size(), 3u) << "the library sidebar must still list this app's own build";
}

TEST_F(ChannelFlowTest, PluginInstrumentLoadCompletingAfterNewPatchIsDroppedNotAddedToTheFreshDocument) {
    // SHOULD-FIX (finding 4): a plugin instrument load still in flight when New Patch replaces the
    // document must not land as a track in the FRESH document once it completes.
    InstrumentPluginStubBackendCFT backend;
    backend.factories["Stub Synth"] = [] {
        return std::make_unique<synth::test::StubPluginInstance>(0, 2, "Stub Synth");
    };
    synth::HostedPluginBackend::ScopedDefault installed(&backend);

    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    auto& graph = mc.getAudioEngine().getGraph();
    auto& doc = mc.getTimelineDoc();
    mc.getPluginScanService().setCandidateSource([](const juce::String&) { return juce::StringArray(); });
    seedScanListCFT(mc.getPluginScanService(), {pluginDescriptionCFT("Stub Synth", 0xA1FA, /*isInstrument=*/true)});

    mc.getTimelinePanel().buildAddTrackMenu();
    mc.getTimelinePanel().applyAddTrackMenuChoice(synth::ui::TimelinePanelComponent::kAddInstrumentPluginMenuIdBase +
                                                  0);
    // The load is now in flight (InstrumentPluginStubBackendCFT completes via callAsync, i.e. a
    // later message-loop turn) — nothing has touched the graph or doc yet, since
    // buildInstrumentTrackAndChain only ever runs once onLoadCompleted fires.
    ASSERT_TRUE(doc.getTracks().empty());

    // New Patch — the REAL guarded flow (guardUnsavedChanges("New Patch", ...), same seam a menu
    // click or Cmd+N goes through) — replaces the document WHILE that load is still pending. Let
    // New Patch's OWN (unrelated) undo-tracked steps settle before touching the stale load at all,
    // so the edit-serial comparison below isolates just the stale completion's effect.
    mc.simulateNewPatchClick();
    pumpUntilCFT([] { return false; }, 50);
    const int editSerialAfterNewPatchSettled = mc.getUndoManager().getEditSerial();

    // Now let the stale load's completion land. There is deliberately nothing to poll FOR beyond
    // that: a correctly dropped completion has no observable side effect at all, which is exactly
    // what this proves.
    pumpUntilCFT([] { return false; }, 300);

    EXPECT_TRUE(doc.getTracks().empty()) << "the stale load must not land a track in the fresh document";
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::HostedPlugin), 0)
        << "the stale plugin instance must never join the fresh document's graph";
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::ChannelStrip), 0);
    EXPECT_EQ(mc.getUndoManager().getEditSerial(), editSerialAfterNewPatchSettled)
        << "the dropped completion must push no undo step of its own, on top of New Patch's own";
}

TEST_F(ChannelFlowTest, PluginInstrumentTrackMonoInstanceDuplicatesOntoBothChannelLegs) {
    // TEST GAP (finding 5a): the real menu -> applyAddTrackMenuChoice -> buildInstrumentTrackAndChain
    // path for a genuinely MONO instrument instance. HostedPluginModule::rightAudioLegChannel()
    // reads 0 (not -1, not a real ch1) once the published instance has exactly one real output —
    // see HostedPluginTest.RightAudioLegChannelFollowsThePublishedInstancesRealOutputCount for the
    // module-level contract this proves end-to-end through the real menu/chain-build path.
    InstrumentPluginStubBackendCFT backend;
    backend.factories["Mono Synth"] = [] {
        return std::make_unique<synth::test::StubPluginInstance>(0, 1, "Mono Synth");
    };
    synth::HostedPluginBackend::ScopedDefault installed(&backend);

    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    auto& graph = mc.getAudioEngine().getGraph();
    mc.getPluginScanService().setCandidateSource([](const juce::String&) { return juce::StringArray(); });
    seedScanListCFT(mc.getPluginScanService(), {pluginDescriptionCFT("Mono Synth", 0x3001, /*isInstrument=*/true)});

    mc.getTimelinePanel().buildAddTrackMenu();
    mc.getTimelinePanel().applyAddTrackMenuChoice(synth::ui::TimelinePanelComponent::kAddInstrumentPluginMenuIdBase +
                                                  0);
    ASSERT_TRUE(pumpUntilCFT([&] { return countNodesOfTypeCFT(graph, ModuleType::ChannelStrip) == 1; }))
        << "the async load/chain-build never completed";

    auto* plugin = findNodeOfTypeCFT(graph, ModuleType::HostedPlugin);
    auto* eq = findNodeOfTypeCFT(graph, ModuleType::ParametricEQ);
    ASSERT_NE(plugin, nullptr);
    ASSERT_NE(eq, nullptr);
    auto* hostedModule = dynamic_cast<ModuleBase*>(plugin->getProcessor());
    ASSERT_NE(hostedModule, nullptr);
    EXPECT_EQ(hostedModule->rightAudioLegChannel(), 0) << "a genuinely mono instance's right leg is its own ch0";

    bool leftWired = false, rightFromCh0Wired = false, rightFromCh1Wired = false;
    for (const auto& c : graph.getConnections()) {
        if (c.source.nodeID != plugin->nodeID || c.destination.nodeID != eq->nodeID)
            continue;
        if (c.source.channelIndex == 0 && c.destination.channelIndex == 0)
            leftWired = true;
        if (c.source.channelIndex == 0 && c.destination.channelIndex == 1)
            rightFromCh0Wired = true;
        if (c.source.channelIndex == 1 && c.destination.channelIndex == 1)
            rightFromCh1Wired = true;
    }
    EXPECT_TRUE(leftWired) << "the plugin's one real output must reach EQ L";
    EXPECT_TRUE(rightFromCh0Wired) << "the SAME ch0 output must also reach EQ R — a mono instance duplicates "
                                      "onto both legs rather than going silent on the right";
    EXPECT_FALSE(rightFromCh1Wired) << "there is no real ch1 to wire from on a mono instance";
}

TEST_F(ChannelFlowTest, PluginInstrumentTrackOverMaxChannelsIsRefusedGraphAndUndoUntouched) {
    // TEST GAP (finding 5b): a plugin instance whose real channel count exceeds
    // HostedPluginModule::kMaxPluginChannels is REFUSED inside publishInstance(), never truncated
    // (Source/Modules/CLAUDE.md's invariant). hasInstance() reads that refusal identically to an
    // outright backend failure, so this must behave exactly like
    // PluginInstrumentTrackFailedLoadLeavesGraphAndUndoUntouched: no track, no node, no undo step.
    InstrumentPluginStubBackendCFT backend;
    backend.factories["Huge Synth"] = [] {
        return std::make_unique<synth::test::StubPluginInstance>(0, synth::HostedPluginModule::kMaxPluginChannels + 1,
                                                                 "Huge Synth");
    };
    synth::HostedPluginBackend::ScopedDefault installed(&backend);

    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    auto& graph = mc.getAudioEngine().getGraph();
    auto& doc = mc.getTimelineDoc();
    mc.getPluginScanService().setCandidateSource([](const juce::String&) { return juce::StringArray(); });
    seedScanListCFT(mc.getPluginScanService(), {pluginDescriptionCFT("Huge Synth", 0x4001, /*isInstrument=*/true)});

    const int tracksBefore = (int)doc.getTracks().size();
    const int nodesBefore = graph.getNumNodes();
    const juce::String messageBefore = mc.getStatusBar().getTransientMessageForTest();

    mc.getTimelinePanel().buildAddTrackMenu();
    mc.getTimelinePanel().applyAddTrackMenuChoice(synth::ui::TimelinePanelComponent::kAddInstrumentPluginMenuIdBase +
                                                  0);

    ASSERT_TRUE(pumpUntilCFT([&] { return mc.getStatusBar().getTransientMessageForTest() != messageBefore; }))
        << "the refusal was never reported";

    EXPECT_EQ((int)doc.getTracks().size(), tracksBefore) << "an over-max instance must leave no orphan track";
    EXPECT_EQ(graph.getNumNodes(), nodesBefore) << "an over-max instance must leave no orphan node — not even a "
                                                   "bare Hosted Plugin placeholder";
    EXPECT_FALSE(mc.getUndoManager().canUndo()) << "an over-max refusal must push no undo step";

    // Same later-turn teardown as the failed-load test above.
    pumpUntilCFT([] { return false; }, 100);
    EXPECT_EQ((int)doc.getTracks().size(), tracksBefore);
    EXPECT_EQ(graph.getNumNodes(), nodesBefore);
    EXPECT_FALSE(mc.getUndoManager().canUndo());
}

// synth::addVoiceMixerForPolyInstrument / the poly branch of MainComponent::addInstrumentTrack's
// chain-source selection, exercised directly at the ChannelFlows level: a factory-default
// Oscillator is poly OFF (see InstrumentTrackOscillatorWiresSplitBlockRightLegNeverCh1 above), so
// the golden "+ Track -> Instrument" path never takes this branch today — this proves it wires
// correctly for whenever an instrument IS poly (docs/mixer.md §5.4/§5.8).
TEST_F(ChannelFlowTest, PolyInstrumentGetsVoiceMixerAheadOfStripAndFeedsTheChannel) {
    AudioEngine engine;
    auto& graph = engine.getGraph();

    auto oscProcessor = synth::AIStateMapper::createModule("Oscillator");
    ASSERT_NE(oscProcessor, nullptr);
    ASSERT_TRUE(setPolyParamCFT(oscProcessor.get(), true));
    auto oscNode = graph.addNode(std::move(oscProcessor));
    ASSERT_NE(oscNode, nullptr);

    juce::String voiceMixerUuid;
    auto* voiceMixer = synth::addVoiceMixerForPolyInstrument(graph, *oscNode, {0, 0}, voiceMixerUuid);
    ASSERT_NE(voiceMixer, nullptr);
    EXPECT_FALSE(voiceMixerUuid.isEmpty());
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::VoiceMixer), 1);

    for (int voice = 0; voice < 8; ++voice)
        EXPECT_TRUE(graph.isConnected({{oscNode->nodeID, voice}, {voiceMixer->nodeID, voice}}))
            << "voice " << voice << " must be summed into the Voice Mixer";

    const synth::DefaultChannelLayout layout{{100, 0}, {200, 0}, {300, 0}, {400, 0}};
    const auto channel = synth::buildDefaultAudioChannel(graph, *voiceMixer, layout);
    ASSERT_FALSE(channel.stripUuid.isEmpty());

    auto* eq = findNodeOfTypeCFT(graph, ModuleType::ParametricEQ);
    ASSERT_NE(eq, nullptr);
    EXPECT_TRUE(graph.isConnected({{voiceMixer->nodeID, 0}, {eq->nodeID, 0}}))
        << "Voice Mixer's own ch0/ch1 output satisfies buildDefaultAudioChannel's default contiguous-pair contract";
    EXPECT_TRUE(graph.isConnected({{voiceMixer->nodeID, 1}, {eq->nodeID, 1}}));
}

TEST_F(ChannelFlowTest, NonPolyInstrumentGetsNoVoiceMixer) {
    AudioEngine engine;
    auto& graph = engine.getGraph();

    auto oscProcessor = synth::AIStateMapper::createModule("Oscillator");
    ASSERT_NE(oscProcessor, nullptr);
    // Factory default: poly OFF — no setPolyParamCFT call.
    auto oscNode = graph.addNode(std::move(oscProcessor));
    ASSERT_NE(oscNode, nullptr);

    juce::String voiceMixerUuid;
    auto* voiceMixer = synth::addVoiceMixerForPolyInstrument(graph, *oscNode, {0, 0}, voiceMixerUuid);
    EXPECT_EQ(voiceMixer, nullptr);
    EXPECT_TRUE(voiceMixerUuid.isEmpty());
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::VoiceMixer), 0);
}

// synth::addEnvelopeAndVCAForRawInstrument, exercised directly at the ChannelFlows level for the
// poly case (a factory-default Oscillator is poly OFF, so the golden "+ Track -> Instrument" path
// never exercises composition with a Voice Mixer today — see PolyInstrumentGetsVoiceMixerAheadOf-
// StripAndFeedsTheChannel above for the same reasoning). Proves the VCA is inserted AFTER the Voice
// Mixer's poly-voice sum, both forced non-poly, exactly as MainComponent::addInstrumentTrack does.
TEST_F(ChannelFlowTest, EnvelopeAndVCAComposeAfterVoiceMixerForPolyInstrument) {
    AudioEngine engine;
    auto& graph = engine.getGraph();

    auto trackInProcessor = synth::AIStateMapper::createModule("Track In");
    ASSERT_NE(trackInProcessor, nullptr);
    auto trackInNode = graph.addNode(std::move(trackInProcessor));
    ASSERT_NE(trackInNode, nullptr);

    auto oscProcessor = synth::AIStateMapper::createModule("Oscillator");
    ASSERT_NE(oscProcessor, nullptr);
    ASSERT_TRUE(setPolyParamCFT(oscProcessor.get(), true));
    auto oscNode = graph.addNode(std::move(oscProcessor));
    ASSERT_NE(oscNode, nullptr);

    juce::String voiceMixerUuid;
    auto* voiceMixer = synth::addVoiceMixerForPolyInstrument(graph, *oscNode, {100, 0}, voiceMixerUuid);
    ASSERT_NE(voiceMixer, nullptr);

    const auto envAndVca = synth::addEnvelopeAndVCAForRawInstrument(graph, *trackInNode, *voiceMixer,
                                                                    /*chainSourceRightChannel=*/1, {200, 0}, {300, 0});
    ASSERT_NE(envAndVca.vca, nullptr);
    EXPECT_FALSE(envAndVca.adsrUuid.isEmpty());
    EXPECT_FALSE(envAndVca.vcaUuid.isEmpty());

    auto* adsrNode = findNodeOfTypeCFT(graph, ModuleType::ADSR);
    ASSERT_NE(adsrNode, nullptr);
    for (auto* param : adsrNode->getProcessor()->getParameters())
        if (auto* boolParam = dynamic_cast<juce::AudioParameterBool*>(param))
            if (boolParam->paramID == "poly")
                EXPECT_FALSE(boolParam->get()) << "the ADSR must stay non-poly even for a poly instrument";
    for (auto* param : envAndVca.vca->getProcessor()->getParameters())
        if (auto* boolParam = dynamic_cast<juce::AudioParameterBool*>(param))
            if (boolParam->paramID == "poly")
                EXPECT_FALSE(boolParam->get()) << "the VCA must stay non-poly even for a poly instrument";

    // Voice Mixer's summed ch0/ch1 -> VCA Audio L/R (never the poly instrument's raw ch0-7 directly).
    EXPECT_TRUE(graph.isConnected({{voiceMixer->nodeID, 0}, {envAndVca.vca->nodeID, 0}}));
    EXPECT_TRUE(graph.isConnected({{voiceMixer->nodeID, 1}, {envAndVca.vca->nodeID, VCAModule::kRightBase}}));

    const synth::DefaultChannelLayout layout{{400, 0}, {500, 0}, {600, 0}, {700, 0}};
    const auto channel = synth::buildDefaultAudioChannel(graph, *envAndVca.vca, layout, VCAModule::kRightBase);
    ASSERT_FALSE(channel.stripUuid.isEmpty()) << "the VCA's output must satisfy buildDefaultAudioChannel too";
}

// FRO46 (P9-3j): synth::addPolyEnvelopeAndVCAForInstrument, exercised directly at the ChannelFlows
// level (a factory-default Oscillator is poly OFF, so the golden "+ Track -> Instrument" path never
// takes this branch today, same reasoning as the poly tests above) — proves a poly instrument gets
// a TRUE per-voice envelope: Poly MIDI's Pitch/Gate fans feed the instrument and a poly ADSR
// directly, no Voice Mixer, both ADSR and VCA stay poly (never forced non-poly).
TEST_F(ChannelFlowTest, PolyEnvelopeAndVCAWiresPerVoicePitchGateAndAudioWithNoVoiceMixer) {
    AudioEngine engine;
    auto& graph = engine.getGraph();

    auto trackInProcessor = synth::AIStateMapper::createModule("Track In");
    ASSERT_NE(trackInProcessor, nullptr);
    auto trackInNode = graph.addNode(std::move(trackInProcessor));
    ASSERT_NE(trackInNode, nullptr);

    auto oscProcessor = synth::AIStateMapper::createModule("Oscillator");
    ASSERT_NE(oscProcessor, nullptr);
    ASSERT_TRUE(setPolyParamCFT(oscProcessor.get(), true));
    auto oscNode = graph.addNode(std::move(oscProcessor));
    ASSERT_NE(oscNode, nullptr);

    const auto polyEnv =
        synth::addPolyEnvelopeAndVCAForInstrument(graph, *trackInNode, *oscNode, {100, 0}, {200, 0}, {300, 0});
    ASSERT_NE(polyEnv.vca, nullptr);
    EXPECT_FALSE(polyEnv.polyMidiUuid.isEmpty());
    EXPECT_FALSE(polyEnv.adsrUuid.isEmpty());
    EXPECT_FALSE(polyEnv.vcaUuid.isEmpty());

    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::PolyMidi), 1);
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::VoiceMixer), 0) << "the poly VCA does its own 8-voice summing";

    auto* polyMidiNode = findNodeOfTypeCFT(graph, ModuleType::PolyMidi);
    auto* adsrNode = findNodeOfTypeCFT(graph, ModuleType::ADSR);
    ASSERT_NE(polyMidiNode, nullptr);
    ASSERT_NE(adsrNode, nullptr);

    for (auto* param : adsrNode->getProcessor()->getParameters()) {
        if (auto* boolParam = dynamic_cast<juce::AudioParameterBool*>(param))
            if (boolParam->paramID == "poly")
                EXPECT_TRUE(boolParam->get()) << "the ADSR must be poly — its gate now comes from Poly MIDI CV";
        if (auto* floatParam = dynamic_cast<juce::AudioParameterFloat*>(param))
            if (floatParam->paramID == "sustain")
                EXPECT_FLOAT_EQ(floatParam->get(), 0.7f) << "same sustain override as the non-poly path";
    }
    for (auto* param : polyEnv.vca->getProcessor()->getParameters()) {
        if (auto* boolParam = dynamic_cast<juce::AudioParameterBool*>(param))
            if (boolParam->paramID == "poly")
                EXPECT_TRUE(boolParam->get()) << "the VCA must be poly";
        if (auto* floatParam = dynamic_cast<juce::AudioParameterFloat*>(param))
            if (floatParam->paramID == "gain")
                EXPECT_FLOAT_EQ(floatParam->get(), 1.0f) << "same gain override as the non-poly path";
    }

    EXPECT_TRUE(graph.isConnected({{trackInNode->nodeID, juce::AudioProcessorGraph::midiChannelIndex},
                                   {polyMidiNode->nodeID, juce::AudioProcessorGraph::midiChannelIndex}}))
        << "Poly MIDI, not the ADSR, must be gated by Track In's MIDI";

    for (int voice = 0; voice < 8; ++voice) {
        EXPECT_TRUE(graph.isConnected({{polyMidiNode->nodeID, voice}, {oscNode->nodeID, voice}}))
            << "voice " << voice << ": Poly MIDI's Pitch fan must feed the instrument's poly Pitch CV";
        EXPECT_TRUE(graph.isConnected({{polyMidiNode->nodeID, 8 + voice}, {adsrNode->nodeID, voice}}))
            << "voice " << voice << ": Poly MIDI's Gate fan must feed the ADSR's poly Gate CV";
        EXPECT_TRUE(
            graph.isConnected({{adsrNode->nodeID, voice}, {polyEnv.vca->nodeID, VCAModule::kPolyCVBase + voice}}))
            << "voice " << voice << ": ADSR's poly Env out must feed the VCA's poly Gain CV";
        EXPECT_TRUE(graph.isConnected({{oscNode->nodeID, voice}, {polyEnv.vca->nodeID, voice}}))
            << "voice " << voice << ": the instrument's poly Audio L must feed the VCA's poly Audio L in";
    }

    const synth::DefaultChannelLayout layout{{400, 0}, {500, 0}, {600, 0}, {700, 0}};
    const auto channel = synth::buildDefaultAudioChannel(graph, *polyEnv.vca, layout, /*sourceRightChannel=*/1);
    ASSERT_FALSE(channel.stripUuid.isEmpty())
        << "the VCA's summed ch0/ch1 output must satisfy buildDefaultAudioChannel";
}

// FRO48 (P9-3k): the "+ Track -> Instrument -> Oscillator (Poly)" menu entry is the first real UI
// entry point for the poly-envelope auto-wire above — it must set the freshly created Oscillator's
// "poly" parameter BEFORE MainComponent::addInstrumentTrack's own isProcessorPoly check runs, so
// the golden "+ Track" path (never poly today — see InstrumentTrackOscillatorWiresSplitBlockRight-
// LegNeverCh1 above) can actually reach PolyEnvelopeAndVCAWiresPerVoicePitchGateAndAudioWithNo-
// VoiceMixer's wiring. Drives the real menu seam (applyAddTrackMenuChoice), not
// MainComponent::addInstrumentTrack or synth::addPolyEnvelopeAndVCAForInstrument directly.
TEST_F(ChannelFlowTest, AddInstrumentTrackMenuOscillatorPolyWiresPolyEnvelopeAndVCA) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    auto& graph = mc.getAudioEngine().getGraph();
    auto& macros = mc.getGraphEditor().getMacros();

    addInstrumentTrack(mc, "Oscillator", true);

    auto* oscillator = findNodeOfTypeCFT(graph, ModuleType::Oscillator);
    ASSERT_NE(oscillator, nullptr);
    EXPECT_TRUE(synth::isProcessorPoly(oscillator->getProcessor()))
        << "the (Poly) menu entry must have turned the instrument's own poly parameter on";

    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::PolyMidi), 1);
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::VoiceMixer), 0)
        << "the poly VCA does its own 8-voice summing — no separate Voice Mixer stage";

    ASSERT_EQ(macros.size(), 1);
    const auto& macro = macros.getAll().front();
    // The factory default preset every fresh MainComponent loads already has its own ADSR-type
    // nodes and a VCA node — disambiguate via macro membership, not "last one seen".
    auto* adsrNode = findMacroMemberOfTypeCFT(graph, macro, ModuleType::ADSR);
    auto* vcaNode = findMacroMemberOfTypeCFT(graph, macro, ModuleType::VCA);
    ASSERT_NE(adsrNode, nullptr);
    ASSERT_NE(vcaNode, nullptr);
    for (auto* param : adsrNode->getProcessor()->getParameters())
        if (auto* boolParam = dynamic_cast<juce::AudioParameterBool*>(param))
            if (boolParam->paramID == "poly")
                EXPECT_TRUE(boolParam->get()) << "the ADSR must be poly — its gate comes from Poly MIDI CV";
    for (auto* param : vcaNode->getProcessor()->getParameters())
        if (auto* boolParam = dynamic_cast<juce::AudioParameterBool*>(param))
            if (boolParam->paramID == "poly")
                EXPECT_TRUE(boolParam->get()) << "the VCA must be poly";

    // The default downstream chain must still be built, exactly as the non-poly path gets.
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::ParametricEQ), 1);
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::Compressor), 1);
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::ChannelStrip), 1);
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::Master), 1);
}

// Same as AddInstrumentTrackMenuOscillatorPolyWiresPolyEnvelopeAndVCA above, for the Wavetable
// entry — Wavetable gets the same poly-envelope auto-wire as Oscillator (P9-3j).
TEST_F(ChannelFlowTest, AddInstrumentTrackMenuWavetablePolyWiresPolyEnvelopeAndVCA) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    auto& graph = mc.getAudioEngine().getGraph();
    auto& macros = mc.getGraphEditor().getMacros();

    addInstrumentTrack(mc, "Wavetable", true);

    auto* wavetable = findNodeOfTypeCFT(graph, ModuleType::Wavetable);
    ASSERT_NE(wavetable, nullptr);
    EXPECT_TRUE(synth::isProcessorPoly(wavetable->getProcessor()))
        << "the (Poly) menu entry must have turned the instrument's own poly parameter on";

    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::PolyMidi), 1);
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::VoiceMixer), 0)
        << "the poly VCA does its own 8-voice summing — no separate Voice Mixer stage";

    ASSERT_EQ(macros.size(), 1);
    const auto& macro = macros.getAll().front();
    auto* adsrNode = findMacroMemberOfTypeCFT(graph, macro, ModuleType::ADSR);
    auto* vcaNode = findMacroMemberOfTypeCFT(graph, macro, ModuleType::VCA);
    ASSERT_NE(adsrNode, nullptr);
    ASSERT_NE(vcaNode, nullptr);
    for (auto* param : adsrNode->getProcessor()->getParameters())
        if (auto* boolParam = dynamic_cast<juce::AudioParameterBool*>(param))
            if (boolParam->paramID == "poly")
                EXPECT_TRUE(boolParam->get()) << "the ADSR must be poly — its gate comes from Poly MIDI CV";
    for (auto* param : vcaNode->getProcessor()->getParameters())
        if (auto* boolParam = dynamic_cast<juce::AudioParameterBool*>(param))
            if (boolParam->paramID == "poly")
                EXPECT_TRUE(boolParam->get()) << "the VCA must be poly";

    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::ParametricEQ), 1);
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::Compressor), 1);
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::ChannelStrip), 1);
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::Master), 1);
}

// Regression guard for the two tests above: the plain (non-poly) "Oscillator" menu entry must not
// have started taking the poly-envelope branch for everyone.
TEST_F(ChannelFlowTest, AddInstrumentTrackMenuOscillatorNonPolyStaysNonPoly) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    auto& graph = mc.getAudioEngine().getGraph();

    addInstrumentTrack(mc, "Oscillator");

    auto* oscillator = findNodeOfTypeCFT(graph, ModuleType::Oscillator);
    ASSERT_NE(oscillator, nullptr);
    EXPECT_FALSE(synth::isProcessorPoly(oscillator->getProcessor()))
        << "the plain menu entry must leave the instrument's poly parameter off";
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::PolyMidi), 0)
        << "the non-poly menu entry must never take the poly-envelope branch";
    EXPECT_EQ(countNodesOfTypeCFT(graph, ModuleType::VoiceMixer), 0)
        << "a freshly created Oscillator defaults to poly OFF — no Voice Mixer needed";
}
