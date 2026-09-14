// Library UX: the sidebar's Plugins section (rows, empty state, format sub-grouping, collapse), the
// scan row wired to a real scan, activating a row, and the drag payload landing a hosted node.

#include "PluginScanTestHelpers.h"

#include "AudioEngine/AudioEngine.h"
#include "Plugin/Hosting/HostedPluginModule.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"

// ============================================================================
// 7. Library UX
// ============================================================================

namespace {

int countRowsOfKind(const ModuleLibraryComponent& library, ModuleLibraryComponent::RowKind kind) {
    int count = 0;
    for (int i = 0; i < library.getEntryCount(); ++i)
        if (library.getEntry(i).kind == kind)
            ++count;
    return count;
}

} // namespace

TEST(PluginScanTest, LibraryPluginsSectionEmptyStateIsTheScanRow) {
    ModuleLibraryComponent library;

    const int header = entryIndexForText(library, ModuleLibraryComponent::kPluginsHeader);
    ASSERT_GE(header, 0) << "the Plugins section must exist even before any scan";
    EXPECT_EQ(library.getEntry(header).kind, ModuleLibraryComponent::RowKind::Header);

    EXPECT_EQ(library.getPluginCount(), 0);
    EXPECT_EQ(countRowsOfKind(library, ModuleLibraryComponent::RowKind::Plugin), 0);
    // Exactly one Action row, and it is what an empty section shows — the hint IS the button.
    EXPECT_EQ(countRowsOfKind(library, ModuleLibraryComponent::RowKind::Action), 1);
    const int scanRow = entryIndexForText(library, ModuleLibraryComponent::kScanPluginsRowText);
    ASSERT_GE(scanRow, 0);
    EXPECT_EQ(library.getEntry(scanRow).section, ModuleLibraryComponent::kPluginsHeader);

    // Plugins are not module types: nothing here may leak into the factory-name list.
    EXPECT_FALSE(library.getDraggableModuleNames().contains(ModuleLibraryComponent::kScanPluginsRowText));
    EXPECT_FALSE(library.getDraggableModuleNames().contains("Hosted Plugin"));
}

TEST(PluginScanTest, LibraryPluginsSectionShowsScannedRows) {
    ModuleLibraryComponent library;

    PluginIdentity alpha;
    alpha.format = "VST3";
    alpha.name = "Alpha";
    alpha.uid = 0xA1FA;
    PluginIdentity beta;
    beta.format = "AudioUnit";
    beta.name = "Beta";
    beta.uid = 0xB37A;
    library.setPlugins({alpha, beta});

    EXPECT_EQ(library.getPluginCount(), 2);
    EXPECT_EQ(countRowsOfKind(library, ModuleLibraryComponent::RowKind::Plugin), 2);
    EXPECT_EQ(countRowsOfKind(library, ModuleLibraryComponent::RowKind::Action), 1)
        << "the scan row stays, so the list can be refreshed";

    const int alphaRow = entryIndexForText(library, "Alpha");
    ASSERT_GE(alphaRow, 0);
    EXPECT_EQ(library.getEntry(alphaRow).section, ModuleLibraryComponent::kPluginsHeader);
    EXPECT_EQ(library.getEntry(alphaRow).detail, "VST3") << "the format tag distinguishes a VST3 from its AU twin";
    EXPECT_EQ(library.getPluginIdentity(alphaRow), alpha);
    EXPECT_TRUE(library.isEntryEnabled(alphaRow)) << "plugin rows are draggable";

    // Still not module types.
    EXPECT_FALSE(library.getDraggableModuleNames().contains("Alpha"));
    EXPECT_EQ(library.getPluginIdentity(entryIndexForText(library, "Oscillator")), PluginIdentity{});

    // ...and the section empties again when a rescan finds nothing.
    library.setPlugins({});
    EXPECT_EQ(countRowsOfKind(library, ModuleLibraryComponent::RowKind::Plugin), 0);
    EXPECT_EQ(countRowsOfKind(library, ModuleLibraryComponent::RowKind::Action), 1);
}

TEST(PluginScanTest, LibraryPluginsSectionSubGroupsRowsByFormat) {
    ModuleLibraryComponent library;

    // Handed in reverse-format order (and not name-sorted within a format) to prove the section
    // does its own alphabetical-by-format grouping rather than trusting caller order.
    PluginIdentity vst3Zeta;
    vst3Zeta.format = "VST3";
    vst3Zeta.name = "Zeta";
    vst3Zeta.uid = 1;
    PluginIdentity vst3Alpha;
    vst3Alpha.format = "VST3";
    vst3Alpha.name = "Alpha";
    vst3Alpha.uid = 2;
    PluginIdentity auBeta;
    auBeta.format = "AudioUnit";
    auBeta.name = "Beta";
    auBeta.uid = 3;
    library.setPlugins({vst3Zeta, vst3Alpha, auBeta});

    // Two formats -> exactly two non-clickable sub-label rows, sorted alphabetically by format
    // ("AudioUnit" before "VST3"), each carrying only its format name and living in the Plugins
    // section.
    EXPECT_EQ(countRowsOfKind(library, ModuleLibraryComponent::RowKind::SubHeader), 2);

    juce::StringArray subHeaderTextsInOrder;
    for (int i = 0; i < library.getEntryCount(); ++i)
        if (library.getEntry(i).kind == ModuleLibraryComponent::RowKind::SubHeader)
            subHeaderTextsInOrder.add(library.getEntry(i).text);
    ASSERT_EQ(subHeaderTextsInOrder.size(), 2);
    EXPECT_EQ(subHeaderTextsInOrder[0], "AudioUnit");
    EXPECT_EQ(subHeaderTextsInOrder[1], "VST3");

    const int auSubHeader = entryIndexForText(library, "AudioUnit");
    const int vst3SubHeader = entryIndexForText(library, "VST3");
    ASSERT_GE(auSubHeader, 0);
    ASSERT_GE(vst3SubHeader, 0);
    EXPECT_EQ(library.getEntry(auSubHeader).section, ModuleLibraryComponent::kPluginsHeader);
    EXPECT_EQ(library.getEntry(vst3SubHeader).section, ModuleLibraryComponent::kPluginsHeader);

    // The AudioUnit group (one row) comes entirely before the VST3 group (two rows, name-sorted).
    const int betaRow = entryIndexForText(library, "Beta");
    const int alphaRow = entryIndexForText(library, "Alpha");
    const int zetaRow = entryIndexForText(library, "Zeta");
    ASSERT_GE(betaRow, 0);
    ASSERT_GE(alphaRow, 0);
    ASSERT_GE(zetaRow, 0);
    EXPECT_LT(auSubHeader, betaRow);
    EXPECT_LT(betaRow, vst3SubHeader);
    EXPECT_LT(vst3SubHeader, alphaRow);
    EXPECT_LT(alphaRow, zetaRow);

    // A sub-label is not draggable, not the click-activated scan row, and not a header — it must
    // not participate in section-collapse row accounting or keyboard/hover interaction.
    EXPECT_FALSE(library.isEntryEnabled(auSubHeader));
    EXPECT_EQ(library.getPluginIdentity(auSubHeader), PluginIdentity{});
}

TEST(PluginScanTest, SubHeaderTogglesIndependentlyOfHeader) {
    ModuleLibraryComponent library;
    library.setSize(200, 1200);

    PluginIdentity vst3Alpha;
    vst3Alpha.format = "VST3";
    vst3Alpha.name = "Alpha";
    vst3Alpha.uid = 1;
    PluginIdentity auBeta;
    auBeta.format = "AudioUnit";
    auBeta.name = "Beta";
    auBeta.uid = 2;
    library.setPlugins({vst3Alpha, auBeta});

    const int vst3SubHeader = entryIndexForText(library, "VST3");
    ASSERT_GE(vst3SubHeader, 0);
    const int y = library.getRowCentreY(vst3SubHeader);
    ASSERT_GT(y, 0);

    // Click the VST3 sub-header row — mirrors real usage, going through mouseDown rather than
    // reaching into the collapse-state key directly.
    juce::MouseInputSource src = juce::Desktop::getInstance().getMainMouseSource();
    juce::MouseEvent evt(src, juce::Point<float>(30.0f, (float)y), juce::ModifierKeys(), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                         &library, &library, juce::Time::getCurrentTime(), juce::Point<float>(30.0f, (float)y),
                         juce::Time::getCurrentTime(), 1, false);
    library.mouseDown(evt);

    // Only the VST3 format's plugin rows disappear from the visible layout...
    EXPECT_EQ(library.getRowCentreY(entryIndexForText(library, "Alpha")), -1);
    // ...while the AudioUnit group and the Plugins header itself stay expanded.
    EXPECT_GT(library.getRowCentreY(entryIndexForText(library, "Beta")), 0);
    EXPECT_GT(library.getRowCentreY(entryIndexForText(library, "AudioUnit")), 0);
    EXPECT_FALSE(library.isSectionCollapsed(ModuleLibraryComponent::kPluginsHeader));

    // The sub-header row itself never disappears — only its own header can hide it.
    EXPECT_GT(library.getRowCentreY(vst3SubHeader), 0);

    // No focus-grabbing side effect (regression guard for #232): a click on the sub-header must not
    // move real keyboard focus onto the searchEditor child, clearing its placeholder text. #232's
    // actual fix is setMouseClickGrabsKeyboardFocus(false) on the panel, asserted directly here.
    // (Since T159, the panel DOES call setWantsKeyboardFocus(true) on itself -- deliberately, so a
    // direct/programmatic grabKeyboardFocus() lands deterministically on the panel root for the
    // "library" focus region -- but that flag is orthogonal to the click path #232 fixed: JUCE
    // checks setMouseClickGrabsKeyboardFocus first and unconditionally, before it ever looks at
    // wantsKeyboardFocus, so this panel's own flag staying false is what actually keeps a click safe.)
    EXPECT_FALSE(library.getMouseClickGrabsKeyboardFocus());
}

TEST(PluginScanTest, SubHeaderCollapseSurvivesCollapseAll) {
    ModuleLibraryComponent library;
    library.setSize(200, 1200);

    PluginIdentity vst3Alpha;
    vst3Alpha.format = "VST3";
    vst3Alpha.name = "Alpha";
    vst3Alpha.uid = 1;
    PluginIdentity auBeta;
    auBeta.format = "AudioUnit";
    auBeta.name = "Beta";
    auBeta.uid = 2;
    library.setPlugins({vst3Alpha, auBeta});

    const int vst3SubHeader = entryIndexForText(library, "VST3");
    ASSERT_GE(vst3SubHeader, 0);
    const int y = library.getRowCentreY(vst3SubHeader);
    ASSERT_GT(y, 0);

    juce::MouseInputSource src = juce::Desktop::getInstance().getMainMouseSource();
    juce::MouseEvent evt(src, juce::Point<float>(30.0f, (float)y), juce::ModifierKeys(), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                         &library, &library, juce::Time::getCurrentTime(), juce::Point<float>(30.0f, (float)y),
                         juce::Time::getCurrentTime(), 1, false);
    library.mouseDown(evt);
    EXPECT_EQ(library.getRowCentreY(entryIndexForText(library, "Alpha")), -1);

    library.toggleAllSections(); // collapse everything
    library.toggleAllSections(); // ...then expand everything

    // The VST3 sub-header's own fold — set by the user before collapse-all ran — must come back
    // exactly as left, not reset by setAllSectionsCollapsed()'s header-only sweep.
    EXPECT_EQ(library.getRowCentreY(entryIndexForText(library, "Alpha")), -1)
        << "the user's own sub-header fold must survive a collapse-all/expand-all round trip";
    EXPECT_GT(library.getRowCentreY(entryIndexForText(library, "Beta")), 0);
}

TEST(PluginScanTest, LibrarySnippetsEmptyHintIsUnaffectedByThePluginsSection) {
    // The Plugins section uses its own row kind precisely so it does not perturb the Snippets
    // section's single EmptyHint row (or any of the counts built on it).
    ModuleLibraryComponent library;
    EXPECT_EQ(countRowsOfKind(library, ModuleLibraryComponent::RowKind::EmptyHint), 1);
}

TEST(PluginScanTest, LibraryScanRowFiresTheScanAndCompletionRefreshesTheRows) {
    ModuleLibraryComponent library;

    FakeLauncher launcher;
    launcher.xmlByFile[kAlpha] = descriptionXml("Alpha", 0xA1FA, kAlpha);
    launcher.xmlByFile[kBeta] = descriptionXml("Beta", 0xB37A, kBeta);

    PluginScanService service;
    service.setCandidateSource(candidates({kAlpha, kBeta}));
    service.setChildLauncher(launcher.fn());

    // The owner's wiring, in miniature: the row asks, the service scans, completion refreshes.
    std::vector<juce::String> progressSeen;
    bool done = false;
    library.onScanPluginsRequested = [&] {
        service.scanAsync(
            juce::StringArray("VST3"), [&](const juce::String& what, int, int) { progressSeen.push_back(what); },
            [&](const PluginScanService::Result&) {
                library.setPlugins(service.getKnownPluginIdentities());
                done = true;
            });
    };

    const int scanRow = entryIndexForText(library, ModuleLibraryComponent::kScanPluginsRowText);
    ASSERT_GE(scanRow, 0);
    ASSERT_EQ(library.getPluginCount(), 0);

    library.activateRow(scanRow);
    ASSERT_TRUE(pumpUntil([&] { return done; }));

    EXPECT_EQ(library.getPluginCount(), 2) << "the completion refresh must reach the rows";
    EXPECT_GE(entryIndexForText(library, "Alpha"), 0);
    // Progress arrives on the message thread, once per candidate.
    EXPECT_EQ(progressSeen.size(), 2u);
}

TEST(PluginScanTest, ActivatingAPluginRowAsksTheOwnerToAddIt) {
    ModuleLibraryComponent library;

    PluginIdentity alpha;
    alpha.format = "VST3";
    alpha.name = "Alpha";
    alpha.uid = 0xA1FA;
    library.setPlugins({alpha});

    std::vector<PluginIdentity> activated;
    library.onPluginActivated = [&](const PluginIdentity& identity) { activated.push_back(identity); };

    library.activateRow(entryIndexForText(library, "Alpha"));
    ASSERT_EQ(activated.size(), 1u);
    EXPECT_EQ(activated[0], alpha);

    // Non-plugin rows never fire it.
    library.activateRow(entryIndexForText(library, "Oscillator"));
    library.activateRow(entryIndexForText(library, ModuleLibraryComponent::kPluginsHeader));
    library.activateRow(-1);
    library.activateRow(9999);
    EXPECT_EQ(activated.size(), 1u);
}

TEST(PluginScanTest, DroppingAPluginPayloadAddsAHostedPluginNode) {
    ScanningStubBackend backend;
    HostedPluginBackend::ScopedDefault installed(&backend);

    PluginScanService service;
    FakeLauncher launcher;
    launcher.xmlByFile[kAlpha] = descriptionXml("Alpha", 0xA1FA, kAlpha);
    service.setCandidateSource(candidates({kAlpha}));
    service.setChildLauncher(launcher.fn());
    scanToCompletion(service);
    backend.setScanService(&service);

    AudioEngine engine;
    GraphEditor editor(engine);

    PluginIdentity alpha;
    alpha.format = "VST3";
    alpha.name = "Alpha";
    alpha.uid = 0xA1FA;
    editor.addHostedPluginAtCanvasPosition(alpha, {100, 100});

    HostedPluginModule* added = nullptr;
    for (auto* node : engine.getGraph().getNodes())
        if (auto* candidate = dynamic_cast<HostedPluginModule*>(node->getProcessor()))
            added = candidate;
    ASSERT_NE(added, nullptr) << "the drop must create a Hosted Plugin node";

    // The identity is set synchronously, BEFORE the node joins the graph — which is what puts it
    // inside the undo snapshot, so a redo brings back the same plugin rather than a bare module.
    EXPECT_EQ(added->getIdentity(), alpha);
    ASSERT_TRUE(pumpUntil([&] { return added->hasInstance(); })) << "the drop never resolved through the scan list";

    // An invalid identity is refused rather than producing a bare module nobody asked for.
    const int nodesBefore = engine.getGraph().getNumNodes();
    editor.addHostedPluginAtCanvasPosition(PluginIdentity{}, {200, 200});
    EXPECT_EQ(engine.getGraph().getNumNodes(), nodesBefore);

    backend.setScanService(nullptr);
}
