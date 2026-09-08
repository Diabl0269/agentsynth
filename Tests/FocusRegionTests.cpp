// T159: the app-wide keyboard focus-region framework (Source/UI/FocusRegion.h) — Tab/Shift+Tab
// cycling between whichever regions are currently open, plus two direct-focus shortcuts
// (Cmd+Shift+T/L) that open a closed target first. First of a 3-part epic; T160/T161 (module-library
// and timeline-track-header navigation WITHIN a region) are NOT covered here.
//
// Two layers of test, matching the split FocusRegion.h's own header comment calls out:
//
//  1. FocusRegionRegistryTest — the registry's PURE decision logic (regionContaining,
//     nextOpenRegionId, focusRegionById's open-before-focus behaviour), built with plain
//     juce::Component stand-ins. None of this needs a native peer or real OS keyboard focus, so it
//     is exercised directly rather than through MainComponent.
//  2. FocusRegionMainComponentTest — the production wiring: which regions MainComponent actually
//     registers, welcome-screen suppression of Tab-cycling, and the two direct-focus commands
//     opening a closed region before dispatching. A real grabKeyboardFocus() needs a native peer
//     this suite has never created (see FocusArbitrationTests.cpp's SurfaceResolverRealFocus for the
//     same constraint), so these tests pin the OBSERVABLE side effects (region open/closed state,
//     command active/inactive, keyPressed's return value) rather than asserting real focus moved.

#include "../Source/AI/AIProvider.h"
#include "../Source/MainComponent.h"
#include "../Source/UI/FocusRegion.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <optional>

// ============================================================================
// 1. FocusRegionRegistry — pure logic, no MainComponent involved.
// ============================================================================

TEST(FocusRegionRegistryTest, RegionContainingFindsRootOrDescendantNeverAStranger) {
    juce::Component libraryRoot, canvasRoot, libraryChild, stranger;
    libraryRoot.addChildComponent(libraryChild);

    synth::ui::FocusRegionRegistry reg;
    reg.addRegion({"library", &libraryRoot, nullptr, nullptr});
    reg.addRegion({"canvas", &canvasRoot, nullptr, nullptr});

    EXPECT_EQ(reg.regionContaining(&libraryRoot), reg.findById("library"));
    EXPECT_EQ(reg.regionContaining(&libraryChild), reg.findById("library"))
        << "focus on a DESCENDANT of a region's root still counts as being in that region";
    EXPECT_EQ(reg.regionContaining(&canvasRoot), reg.findById("canvas"));
    EXPECT_EQ(reg.regionContaining(&stranger), nullptr);
    EXPECT_EQ(reg.regionContaining(nullptr), nullptr);
}

// Regions can NEST -- production has Mod Matrix as a child component of the Graph Canvas -- so
// regionContaining must prefer the MOST SPECIFIC match (the region whose root is itself contained by
// the other candidate's root), never just the first one registered that happens to contain the
// component. Otherwise focus inside the Mod Matrix would always be reported as "canvas".
TEST(FocusRegionRegistryTest, RegionContainingPrefersTheMostSpecificNestedRegion) {
    juce::Component canvasRoot, modMatrixRoot, modMatrixChild;
    canvasRoot.addChildComponent(modMatrixRoot);
    modMatrixRoot.addChildComponent(modMatrixChild);

    synth::ui::FocusRegionRegistry reg;
    // Registered in the SAME order production uses: the outer region ("canvas") before the nested
    // one ("modMatrix") -- if regionContaining just returned the first match, this would fail.
    reg.addRegion({"canvas", &canvasRoot, nullptr, nullptr});
    reg.addRegion({"modMatrix", &modMatrixRoot, nullptr, nullptr});

    EXPECT_EQ(reg.regionContaining(&modMatrixRoot), reg.findById("modMatrix"));
    EXPECT_EQ(reg.regionContaining(&modMatrixChild), reg.findById("modMatrix"))
        << "a descendant of the NESTED region must resolve to the nested region, not the outer one";
    EXPECT_EQ(reg.regionContaining(&canvasRoot), reg.findById("canvas"))
        << "focus on the outer root itself (not inside the nested region) still resolves to the outer one";
}

TEST(FocusRegionRegistryTest, NullIsOpenMeansAlwaysOpen) {
    juce::Component root;
    synth::ui::FocusRegion region{"canvas", &root, nullptr, nullptr};
    EXPECT_TRUE(region.isCurrentlyOpen());
}

// The acceptance criterion this task calls out explicitly: Tab-cycling only ever visits regions
// that are currently OPEN, skipping closed ones entirely, and wraps at both ends.
TEST(FocusRegionRegistryTest, NextOpenRegionIdSkipsClosedRegionsAndWrapsBothWays) {
    synth::ui::FocusRegionRegistry reg;
    juce::Component a, b, c, d;
    bool bOpen = false;
    reg.addRegion({"a", &a, nullptr, nullptr});               // always open
    reg.addRegion({"b", &b, [&] { return bOpen; }, nullptr}); // CLOSED
    reg.addRegion({"c", &c, [] { return true; }, nullptr});   // open
    reg.addRegion({"d", &d, nullptr, nullptr});               // always open

    // Open regions, in registration order: a, c, d ("b" is skipped entirely).
    EXPECT_EQ(reg.nextOpenRegionId("a", true), "c");
    EXPECT_EQ(reg.nextOpenRegionId("c", true), "d");
    EXPECT_EQ(reg.nextOpenRegionId("d", true), "a") << "wraps forward past the end";
    EXPECT_EQ(reg.nextOpenRegionId("a", false), "d") << "wraps backward past the start";
    EXPECT_EQ(reg.nextOpenRegionId("d", false), "c");
    EXPECT_EQ(reg.nextOpenRegionId("c", false), "a");

    // Opening "b" splices it back into the cycle at its registered position.
    bOpen = true;
    EXPECT_EQ(reg.nextOpenRegionId("a", true), "b");
    EXPECT_EQ(reg.nextOpenRegionId("b", true), "c");
}

TEST(FocusRegionRegistryTest, CurrentIdNamingAClosedOrUnknownRegionFallsBackToTheEdge) {
    synth::ui::FocusRegionRegistry reg;
    juce::Component a, b;
    bool bOpen = false;
    reg.addRegion({"a", &a, nullptr, nullptr});
    reg.addRegion({"b", &b, [&] { return bOpen; }, nullptr});

    // "b" doesn't appear in the open list, so it can never be found as "current" -- same as an
    // empty/unknown id, this starts at the edge rather than crashing or looping.
    EXPECT_EQ(reg.nextOpenRegionId("b", true), "a");
    EXPECT_EQ(reg.nextOpenRegionId("", true), "a");
    EXPECT_EQ(reg.nextOpenRegionId("", false), "a") << "only one region is open, so both directions land on it";
    EXPECT_EQ(reg.nextOpenRegionId("no-such-id", true), "a");
}

TEST(FocusRegionRegistryTest, EveryRegionClosedAnswersAnEmptyString) {
    synth::ui::FocusRegionRegistry reg;
    juce::Component a;
    reg.addRegion({"a", &a, [] { return false; }, nullptr});
    EXPECT_TRUE(reg.nextOpenRegionId("", true).isEmpty());
    EXPECT_TRUE(reg.nextOpenRegionId("", false).isEmpty());
}

TEST(FocusRegionRegistryTest, EmptyRegistryAnswersAnEmptyString) {
    synth::ui::FocusRegionRegistry reg;
    EXPECT_TRUE(reg.nextOpenRegionId("", true).isEmpty());
}

// The direct-focus shortcuts' contract: opening a CLOSED region calls its `open` callback exactly
// once before focusing it.
TEST(FocusRegionRegistryTest, FocusRegionByIdOpensAClosedRegionBeforeFocusing) {
    synth::ui::FocusRegionRegistry reg;
    juce::Component root;
    bool open = false;
    int openCalls = 0;
    reg.addRegion({"x", &root, [&] { return open; },
                   [&] {
                       open = true;
                       ++openCalls;
                   }});

    EXPECT_TRUE(reg.focusRegionById("x"));
    EXPECT_EQ(openCalls, 1);
    EXPECT_TRUE(open);
}

// An already-open region's `open` callback must never fire -- opening something that's already
// open would be a surprising side effect (and, for a real toolbar toggle, would CLOSE it instead).
TEST(FocusRegionRegistryTest, FocusRegionByIdOnAnAlreadyOpenRegionNeverCallsOpen) {
    synth::ui::FocusRegionRegistry reg;
    juce::Component root;
    int openCalls = 0;
    reg.addRegion({"x", &root, [] { return true; }, [&] { ++openCalls; }});

    EXPECT_TRUE(reg.focusRegionById("x"));
    EXPECT_EQ(openCalls, 0);
}

// A region with no `open` callback at all (Canvas: null isOpen; Mod Matrix: no direct-focus
// shortcut targets it) must not crash when asked to focus it while closed.
TEST(FocusRegionRegistryTest, FocusRegionByIdWithNoOpenCallbackStillFocusesAClosedRegion) {
    synth::ui::FocusRegionRegistry reg;
    juce::Component root;
    reg.addRegion({"x", &root, [] { return false; }, nullptr});
    EXPECT_TRUE(reg.focusRegionById("x"));
}

TEST(FocusRegionRegistryTest, FocusRegionByIdUnknownIdReturnsFalse) {
    synth::ui::FocusRegionRegistry reg;
    juce::Component root;
    reg.addRegion({"x", &root, nullptr, nullptr});
    EXPECT_FALSE(reg.focusRegionById("nope"));
}

// ============================================================================
// 2. Production wiring: what MainComponent actually registers, welcome-screen suppression, and the
//    two direct-focus commands.
// ============================================================================

namespace {

// Never touches the network -- mirrors FocusArbitrationTests.cpp's FocusMockProvider exactly (this
// codebase's convention is a small per-file mock rather than a shared test-only library).
class FocusRegionMockProvider : public synth::AIProvider {
public:
    juce::String getProviderName() const override { return "FocusRegionMock"; }
    void fetchAvailableModels(std::function<void(const juce::StringArray&, bool)> callback) override {
        if (callback)
            callback({}, true);
    }
    RequestId sendPrompt(const std::vector<Message>&, CompletionCallback callback, const juce::var&,
                         std::function<void(const juce::String&)> = {}) override {
        if (callback)
            callback(AIResponse{false, {}, {}, {}});
        return {};
    }
    void cancel(RequestId) override {}
    void setModel(const juce::String&) override {}
    juce::String getCurrentModel() const override { return {}; }
    void setRequestTimeoutMs(int timeoutMs) override { requestTimeoutMs = timeoutMs; }
    int getRequestTimeoutMs() const override { return requestTimeoutMs; }

private:
    int requestTimeoutMs = 240000;
};

// The ONE on-disk settings file every MainComponent in this process opens (synth::
// userSettingsOptions()) -- same shape as FocusArbitrationTests.cpp's userSettingsTestOptions() and
// WelcomeScreenTests.cpp's own copy, duplicated here per this file's convention.
juce::PropertiesFile::Options userSettingsTestOptions() {
    juce::PropertiesFile::Options opts;
    opts.applicationName = "Agent Synth";
    opts.folderName = "Agent Synth";
    opts.filenameSuffix = "settings";
    opts.osxLibrarySubFolder = "Application Support";
    opts.storageFormat = juce::PropertiesFile::storeAsXML;
    return opts;
}

void writePref(const char* key, const char* value) {
    juce::ApplicationProperties props;
    props.setStorageParameters(userSettingsTestOptions());
    if (auto* settings = props.getUserSettings()) {
        settings->setValue(key, value);
        settings->saveIfNeeded();
    }
}

// Saves the named keys on construction and restores them EXACTLY on destruction (including "the key
// did not exist at all") -- same idiom as FocusArbitrationTests.cpp's PersistedKeysGuard, needed
// because every test below writes into the SAME real settings file every MainComponent instance in
// this process reads.
class PersistedKeysGuard {
public:
    explicit PersistedKeysGuard(juce::StringArray keys) {
        juce::ApplicationProperties props;
        props.setStorageParameters(userSettingsTestOptions());
        auto* settings = props.getUserSettings();
        for (const auto& key : keys) {
            std::optional<juce::String> value;
            if (settings != nullptr && settings->containsKey(key))
                value = settings->getValue(key);
            saved_.emplace_back(key, value);
        }
    }

    ~PersistedKeysGuard() {
        juce::ApplicationProperties props;
        props.setStorageParameters(userSettingsTestOptions());
        auto* settings = props.getUserSettings();
        if (settings == nullptr)
            return;
        for (const auto& [key, value] : saved_) {
            if (value.has_value())
                settings->setValue(key, *value);
            else
                settings->removeValue(key);
        }
        settings->saveIfNeeded();
    }

private:
    std::vector<std::pair<juce::String, std::optional<juce::String>>> saved_;
};

// True when getCommandInfo reports `cmdId` as enabled -- mirrors FocusArbitrationTests.cpp's
// commandIsActive() exactly.
bool commandIsActive(MainComponent& mc, juce::CommandID cmdId) {
    juce::ApplicationCommandInfo info(cmdId);
    mc.getCommandInfo(cmdId, info);
    return (info.flags & juce::ApplicationCommandInfo::isDisabled) == 0;
}

} // namespace

class FocusRegionMainComponentTest : public ::testing::Test {
protected:
    void SetUp() override {
        guard_.emplace(juce::StringArray{"showWelcomeScreenAtLaunch", "librarySidebarVisible", "timelinePanelVisible",
                                         "aiPanelVisible"});
        // A known baseline every test in this fixture starts from: welcome screen hidden (out of the
        // way for the Tab-cycle/registration tests), Library open, Toolbar/Canvas always open,
        // Timeline/AI Panel/Mod Matrix closed -- i.e. exactly "toolbar" + "library" + "canvas" open
        // at construction.
        writePref("showWelcomeScreenAtLaunch", "0");
        writePref("librarySidebarVisible", "1");
        writePref("timelinePanelVisible", "0");
        writePref("aiPanelVisible", "0");
    }
    void TearDown() override { guard_.reset(); }

private:
    std::optional<PersistedKeysGuard> guard_;
};

// The registry MainComponent builds must be exactly the six T159 phase-1 regions, in the
// documented Tab-cycle order (Toolbar, Library, Canvas, Timeline, AI Panel, Mod Matrix).
TEST_F(FocusRegionMainComponentTest, RegistersExactlyTheSixDocumentedRegionsInOrder) {
    MainComponent mc(std::make_unique<FocusRegionMockProvider>());
    juce::StringArray ids;
    for (const auto& region : mc.getFocusRegionsForTest().getRegions())
        ids.add(region.id);
    EXPECT_EQ(ids, juce::StringArray({"toolbar", "library", "canvas", "timeline", "aiPanel", "modMatrix"}));

    // Every region's root must actually be the live component it claims to wrap.
    auto& regs = mc.getFocusRegionsForTest();
    EXPECT_EQ(regs.findById("toolbar")->root, &mc.getToolbar());
    EXPECT_EQ(regs.findById("canvas")->root, &mc.getGraphEditor());
    EXPECT_EQ(regs.findById("timeline")->root, &mc.getTimelinePanel());
    EXPECT_EQ(regs.findById("aiPanel")->root, &mc.getAiChatComponent());
    EXPECT_EQ(regs.findById("modMatrix")->root, &mc.getGraphEditor().getModMatrix());
}

// Mod Matrix is a CHILD component of the Graph Canvas in production (GraphEditor owns a
// ModMatrixComponent member), so the two regions NEST. regionContaining must resolve focus inside
// the Mod Matrix to "modMatrix", never falling back to the outer "canvas" match -- see
// FocusRegionRegistry::regionContaining's nesting note and GraphEditor::paintOverChildren's matching
// double-outline guard.
TEST_F(FocusRegionMainComponentTest, RegionContainingResolvesTheNestedModMatrixNotTheOuterCanvas) {
    MainComponent mc(std::make_unique<FocusRegionMockProvider>());
    auto& regs = mc.getFocusRegionsForTest();
    auto& modMatrix = mc.getGraphEditor().getModMatrix();

    ASSERT_TRUE(mc.getGraphEditor().isParentOf(&modMatrix)) << "production nesting assumption";
    EXPECT_EQ(regs.regionContaining(&modMatrix), regs.findById("modMatrix"));
    EXPECT_EQ(regs.regionContaining(&mc.getGraphEditor()), regs.findById("canvas"));
}

// grabKeyboardFocus() on a region root only lands ON that root deterministically if the root itself
// has opted into keyboard focus; otherwise juce::Component descends into children by Y/X POSITION
// (not by which child wants focus), which is fragile to depend on for a container whose layout can
// change. Every T159 region root must opt in explicitly -- this is a real regression guard for a bug
// class that would otherwise pass every other test in this file (none of them create a native peer,
// so a real grabKeyboardFocus() call is never exercised end-to-end here).
TEST_F(FocusRegionMainComponentTest, EveryRegionRootWantsKeyboardFocusItself) {
    MainComponent mc(std::make_unique<FocusRegionMockProvider>());
    for (const auto& region : mc.getFocusRegionsForTest().getRegions())
        EXPECT_TRUE(region.root->getWantsKeyboardFocus()) << "region \"" << region.id << "\" root";
}

// The acceptance criterion, exercised against the REAL registered regions and real
// isLibraryVisible/isTimelineVisible-backed getters (no fake Components involved): Tab-cycling only
// ever visits what's open, and opening a previously-closed region splices it back into the cycle.
TEST_F(FocusRegionMainComponentTest, TabCycleOnlyVisitsOpenRegionsAndWraps) {
    MainComponent mc(std::make_unique<FocusRegionMockProvider>());
    auto& regs = mc.getFocusRegionsForTest();

    ASSERT_TRUE(mc.isLibraryConfiguredVisible());
    ASSERT_FALSE(mc.isTimelineConfiguredVisible());
    ASSERT_FALSE(mc.isAiPanelConfiguredVisible());
    ASSERT_FALSE(mc.getGraphEditor().isModMatrixVisible());

    // "toolbar", "library" and "canvas" are open (Toolbar has no closed state, same as Canvas) --
    // everything else is skipped.
    EXPECT_EQ(regs.nextOpenRegionId("toolbar", true), "library");
    EXPECT_EQ(regs.nextOpenRegionId("library", true), "canvas");
    EXPECT_EQ(regs.nextOpenRegionId("canvas", true), "toolbar") << "wraps forward, skipping the three closed ones";
    EXPECT_EQ(regs.nextOpenRegionId("toolbar", false), "canvas") << "wraps backward too";

    // Opening the timeline (via the real command, not a fake) adds it at its registered position.
    ASSERT_TRUE(mc.getCommandManager().invokeDirectly(AppCommands::focusTimeline, false));
    ASSERT_TRUE(mc.isTimelineConfiguredVisible());
    EXPECT_EQ(regs.nextOpenRegionId("canvas", true), "timeline");
    EXPECT_EQ(regs.nextOpenRegionId("timeline", true), "toolbar") << "wraps back to the top";
}

// Tab and Shift+Tab dispatch successfully (there is always at least Library+Canvas open) when the
// welcome screen isn't in the way.
TEST_F(FocusRegionMainComponentTest, KeyPressedTabDispatchesSuccessfullyWhenNotSuppressed) {
    MainComponent mc(std::make_unique<FocusRegionMockProvider>());
    ASSERT_NE(mc.getWelcomeScreenForTest(), nullptr);
    ASSERT_FALSE(mc.getWelcomeScreenForTest()->isVisible());

    const juce::KeyPress tab(juce::KeyPress::tabKey, juce::ModifierKeys::noModifiers, 0);
    const juce::KeyPress shiftTab(juce::KeyPress::tabKey, juce::ModifierKeys::shiftModifier, 0);
    EXPECT_TRUE(mc.keyPressed(tab));
    EXPECT_TRUE(mc.keyPressed(shiftTab));
}

// The locked T159 decision: Tab-cycling is suppressed entirely while the launch overlay is up front.
TEST_F(FocusRegionMainComponentTest, TabCyclingSuppressedWhileWelcomeScreenIsVisible) {
    MainComponent mc(std::make_unique<FocusRegionMockProvider>());
    ASSERT_NE(mc.getWelcomeScreenForTest(), nullptr);
    ASSERT_FALSE(mc.getWelcomeScreenForTest()->isVisible()) << "SetUp forced showWelcomeScreenAtLaunch=0";
    EXPECT_TRUE(commandIsActive(mc, AppCommands::focusNextRegion));
    EXPECT_TRUE(commandIsActive(mc, AppCommands::focusPrevRegion));

    mc.getWelcomeScreenForTest()->setVisible(true);
    EXPECT_FALSE(commandIsActive(mc, AppCommands::focusNextRegion));
    EXPECT_FALSE(commandIsActive(mc, AppCommands::focusPrevRegion));

    // An inactive ApplicationCommand must REFUSE to invoke (tryToInvoke checks isCommandActive
    // before calling perform()) -- this is what makes the suppression a real no-op, not merely a
    // greyed-out menu row that still fires when reached by key.
    EXPECT_FALSE(mc.getCommandManager().invokeDirectly(AppCommands::focusNextRegion, false));
    EXPECT_FALSE(mc.getCommandManager().invokeDirectly(AppCommands::focusPrevRegion, false));

    // The full keyPressed() path agrees: a bare Tab reaching the sole dispatch point while the
    // overlay is up front is left unhandled (returns false) rather than swallowed.
    const juce::KeyPress tab(juce::KeyPress::tabKey, juce::ModifierKeys::noModifiers, 0);
    EXPECT_FALSE(mc.keyPressed(tab));

    // Hiding the overlay again un-suppresses both actions.
    mc.getWelcomeScreenForTest()->setVisible(false);
    EXPECT_TRUE(commandIsActive(mc, AppCommands::focusNextRegion));
    EXPECT_TRUE(commandIsActive(mc, AppCommands::focusPrevRegion));
}

// Cmd+Shift+T: opens the Timeline panel if it starts closed, then dispatches successfully. A real
// grabKeyboardFocus() needs a native peer this suite never creates (see
// FocusArbitrationTests.cpp's SurfaceResolverRealFocus), so this pins the OBSERVABLE open/dispatch
// side effects rather than asserting real OS focus moved.
TEST_F(FocusRegionMainComponentTest, CmdShiftTOpensTheTimelinePanelIfClosedThenDispatches) {
    MainComponent mc(std::make_unique<FocusRegionMockProvider>());
    ASSERT_FALSE(mc.isTimelineConfiguredVisible()) << "SetUp forced timelinePanelVisible=0";

    const auto binding = mc.getShortcutManager().getBinding("focusTimeline");
    ASSERT_TRUE(binding.isValid());
    EXPECT_TRUE(mc.keyPressed(binding));
    // MainComponent::keyPressed dispatches via ApplicationCommandManager::invokeDirectly(id, true) --
    // ASYNCHRONOUSLY, like every other command it dispatches (see keyPressed's own loop) -- so
    // perform() runs on the next message-loop pump, not synchronously inside keyPressed() itself.
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    EXPECT_TRUE(mc.isTimelineConfiguredVisible()) << "Cmd+Shift+T must open a closed Timeline panel";
}

// Cmd+Shift+T on an ALREADY-open Timeline must not close it (it is a focus command, not a toggle).
TEST_F(FocusRegionMainComponentTest, CmdShiftTOnAnAlreadyOpenTimelineLeavesItOpen) {
    MainComponent mc(std::make_unique<FocusRegionMockProvider>());
    ASSERT_TRUE(mc.getCommandManager().invokeDirectly(AppCommands::focusTimeline, false));
    ASSERT_TRUE(mc.isTimelineConfiguredVisible());

    EXPECT_TRUE(mc.getCommandManager().invokeDirectly(AppCommands::focusTimeline, false));
    EXPECT_TRUE(mc.isTimelineConfiguredVisible()) << "a second Focus Timeline must not toggle it closed";
}

// Cmd+Shift+L: same contract as Cmd+Shift+T, for the Library sidebar.
TEST_F(FocusRegionMainComponentTest, CmdShiftLOpensTheLibraryIfClosedThenDispatches) {
    writePref("librarySidebarVisible", "0"); // override this fixture's usual "starts open" baseline
    MainComponent mc(std::make_unique<FocusRegionMockProvider>());
    ASSERT_FALSE(mc.isLibraryConfiguredVisible());

    const auto binding = mc.getShortcutManager().getBinding("focusLibrary");
    ASSERT_TRUE(binding.isValid());
    EXPECT_TRUE(mc.keyPressed(binding));
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50); // see the Cmd+Shift+T test's comment
    EXPECT_TRUE(mc.isLibraryConfiguredVisible()) << "Cmd+Shift+L must open a closed Library sidebar";
}

TEST_F(FocusRegionMainComponentTest, CmdShiftLOnAnAlreadyOpenLibraryLeavesItOpen) {
    MainComponent mc(std::make_unique<FocusRegionMockProvider>());
    ASSERT_TRUE(mc.isLibraryConfiguredVisible()) << "SetUp forced librarySidebarVisible=1";

    EXPECT_TRUE(mc.getCommandManager().invokeDirectly(AppCommands::focusLibrary, false));
    EXPECT_TRUE(mc.isLibraryConfiguredVisible()) << "a Focus Library on an already-open sidebar must not close it";
}

// T160: Cmd+F opens the Library first if it is closed, exactly like Cmd+Shift+L above -- but it
// lands on the search field specifically (ModuleLibraryComponent::focusSearchField), not the
// region root, which is not observable headlessly (no native peer to grab real focus against; see
// FocusRegion.h's own comment on that constraint). "Opens if closed" is the one effect this test
// suite CAN observe end-to-end, so it is what's asserted here -- Tests/ModuleLibraryKeyboardNavTests.cpp
// covers the search-field-focused keyboard behaviour this shortcut is FOR.
TEST_F(FocusRegionMainComponentTest, CmdFOpensTheLibraryIfClosedThenDispatches) {
    writePref("librarySidebarVisible", "0"); // override this fixture's usual "starts open" baseline
    MainComponent mc(std::make_unique<FocusRegionMockProvider>());
    ASSERT_FALSE(mc.isLibraryConfiguredVisible());

    const auto binding = mc.getShortcutManager().getBinding("focusLibrarySearch");
    ASSERT_TRUE(binding.isValid());
    EXPECT_TRUE(mc.keyPressed(binding));
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50); // see the Cmd+Shift+T test's comment
    EXPECT_TRUE(mc.isLibraryConfiguredVisible()) << "Cmd+F must open a closed Library sidebar";
}

TEST_F(FocusRegionMainComponentTest, CmdFOnAnAlreadyOpenLibraryLeavesItOpen) {
    MainComponent mc(std::make_unique<FocusRegionMockProvider>());
    ASSERT_TRUE(mc.isLibraryConfiguredVisible()) << "SetUp forced librarySidebarVisible=1";

    EXPECT_TRUE(mc.getCommandManager().invokeDirectly(AppCommands::focusLibrarySearch, false));
    EXPECT_TRUE(mc.isLibraryConfiguredVisible())
        << "a Focus Library Search on an already-open sidebar must not close it";
}

// Cmd+Shift+T/L/Cmd+F are NOT suppressed by the welcome screen -- only Tab-cycling is (per the
// task's own scope: "Suppress ALL TAB CYCLING while welcomeScreen_ is visible").
TEST_F(FocusRegionMainComponentTest, DirectFocusShortcutsAreNotSuppressedByTheWelcomeScreen) {
    MainComponent mc(std::make_unique<FocusRegionMockProvider>());
    ASSERT_NE(mc.getWelcomeScreenForTest(), nullptr);
    mc.getWelcomeScreenForTest()->setVisible(true);

    EXPECT_TRUE(commandIsActive(mc, AppCommands::focusTimeline));
    EXPECT_TRUE(commandIsActive(mc, AppCommands::focusLibrary));
    EXPECT_TRUE(commandIsActive(mc, AppCommands::focusLibrarySearch));
}
