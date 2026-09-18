// DetachRedockStateTests.cpp -- FRO12 (P9-6, docs/mixer/panel.md): proves detach/redock preserves
// REAL, production panel state end to end through MixerDockComponent/MainComponent, not just the
// generic identity/mutation invariant DetachablePanelHostTests.cpp pins against a stub panel.
// Drives a real, off-screen MainComponent (newPatchForTest() + simulateAddAudioTrackClick(), the
// ChannelFlow suite's own rig style -- see MixerPanelComponentTests.cpp).
//
// Both cases below rely on the SAME mechanism: DetachablePanelHost::setDetached() only ever
// REPARENTS panel_ (never rebuilds/recreates it), so anything the panel itself owns survives
// untouched -- unlike MixerPanelComponent::rebuild() (destroys and replaces every column), which
// this test deliberately never calls after the state is set.
//
// FRO101: both tests below build a REAL MainComponent and detach a REAL panel, which means
// DetachedPanelWindow::persistBounds() writes "timelineWindowBounds"/"mixerWindowBounds" into the
// SAME on-disk "Agent Synth" settings file every shipped build reads -- exactly the leak that
// produced the original bug report (a headless test run left a 128x128 rect there, and the
// detached window restored it verbatim on next real launch). Each test wraps itself in a
// PersistedKeysGuard for the one key it touches, same idiom as FocusArbitrationTestFixture.h /
// E2EWorkflowTests.cpp / FocusRegionTests.cpp -- duplicated locally here rather than shared, matching
// how those three already do it.

#include "AI/AIProvider.h"
#include "MainComponent/MainComponent.h"
#include "UI/Mixer/MixerColumnComponent.h"
#include <gtest/gtest.h>
#include <optional>

namespace {

// The ONE on-disk settings file every MainComponent in this process opens (synth::
// userSettingsOptions()) -- same shape as FocusArbitrationTestFixture.h's userSettingsTestOptions().
juce::PropertiesFile::Options userSettingsTestOptions() {
    juce::PropertiesFile::Options opts;
    opts.applicationName = "Agent Synth";
    opts.folderName = "Agent Synth";
    opts.filenameSuffix = "settings";
    opts.osxLibrarySubFolder = "Application Support";
    opts.storageFormat = juce::PropertiesFile::storeAsXML;
    return opts;
}

// Saves the named settings keys on construction and restores them EXACTLY on destruction
// (including "the key did not exist at all") -- same idiom as FocusArbitrationTestFixture.h's
// PersistedKeysGuard, needed because both tests below write into the SAME real settings file
// every MainComponent instance in this process reads.
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

class MockProviderDRST : public synth::AIProvider {
public:
    juce::String getProviderName() const override { return "MockDRST"; }
    void fetchAvailableModels(std::function<void(const juce::StringArray&, bool)> callback) override {
        callback({"MockModel"}, true);
    }
    RequestId sendPrompt(const std::vector<Message>&, CompletionCallback callback, const juce::var&,
                         std::function<void(const juce::String&)> = {}) override {
        AIResponse response;
        response.success = true;
        if (callback)
            callback(response);
        return {};
    }
    void cancel(RequestId) override {}
    void setModel(const juce::String& name) override { model = name; }
    juce::String getCurrentModel() const override { return model; }
    void setRequestTimeoutMs(int timeoutMs) override { requestTimeoutMs = timeoutMs; }
    int getRequestTimeoutMs() const override { return requestTimeoutMs; }

private:
    juce::String model = "MockModel";
    int requestTimeoutMs = 240000;
};

} // namespace

TEST(DetachRedockStateTests, TimelineZoomAndScrollSurviveDetachAndRedock) {
    PersistedKeysGuard guard({"timelineWindowBounds"});

    MainComponent mc(std::make_unique<MockProviderDRST>());
    mc.setSize(1400, 900);
    mc.newPatchForTest();

    auto& viewState = mc.getTimelinePanel().getViewState();
    viewState.pixelsPerBeat = 48.0;
    viewState.firstVisibleBeat = 12.5;

    auto& host = mc.getMixerDock().getTimelineHost();
    host.setDetached(true);
    ASSERT_TRUE(host.isDetached());
    EXPECT_EQ(&mc.getTimelinePanel(), &host.getPanelForTest()) << "the SAME instance, never rebuilt";
    EXPECT_DOUBLE_EQ(viewState.pixelsPerBeat, 48.0) << "zoom must survive being reparented into the window";
    EXPECT_DOUBLE_EQ(viewState.firstVisibleBeat, 12.5) << "scroll position must survive too";

    host.setDetached(false);
    EXPECT_FALSE(host.isDetached());
    EXPECT_DOUBLE_EQ(viewState.pixelsPerBeat, 48.0) << "and survive redocking back";
    EXPECT_DOUBLE_EQ(viewState.firstVisibleBeat, 12.5);
}

TEST(DetachRedockStateTests, MixerColumnSelectionSurvivesDetachAndRedock) {
    PersistedKeysGuard guard({"mixerWindowBounds"});

    MainComponent mc(std::make_unique<MockProviderDRST>());
    mc.setSize(1400, 900);
    mc.newPatchForTest();
    mc.simulateAddAudioTrackClick();

    auto& mixerPanel = mc.getMixerDock().getMixerPanel();
    mixerPanel.rebuild();
    ASSERT_GT(mixerPanel.getColumnCount(), 0);
    auto* column = mixerPanel.getStripColumnForTest(0);
    ASSERT_NE(column, nullptr);

    column->setSelected(true);
    ASSERT_TRUE(column->isSelectedForTest());

    auto& host = mc.getMixerDock().getMixerHost();
    host.setDetached(true);
    ASSERT_TRUE(host.isDetached());
    EXPECT_EQ(&mixerPanel, &host.getPanelForTest()) << "the SAME MixerPanelComponent, never rebuilt";
    // No rebuild() happened -- the same MixerColumnComponent (and its selection flag) is still
    // the one showing, now inside the detached window.
    EXPECT_EQ(mixerPanel.getStripColumnForTest(0), column);
    EXPECT_TRUE(column->isSelectedForTest()) << "selection must survive being reparented into the window";

    host.setDetached(false);
    EXPECT_FALSE(host.isDetached());
    EXPECT_EQ(mixerPanel.getStripColumnForTest(0), column);
    EXPECT_TRUE(column->isSelectedForTest()) << "and survive redocking back";
}

// FRO101 -- proves PersistedKeysGuard itself actually restores the two window-bounds keys, the
// same way PR #328 ("Isolate MainComponent settings-restore tests from the real dev settings
// file") proved its own guard: seed a known value, run production code that would otherwise
// clobber it inside the guard's scope, then read the real file back OUTSIDE the guard and assert
// it matches what was there before. Covers BOTH restorable states named in the guard's own
// contract -- "had a value" and "did not exist at all" -- since a guard that writes back "" instead
// of calling removeValue() would still pass a same-value-only check.
namespace {
void writeRealKey(const juce::String& key, const juce::String& value) {
    juce::ApplicationProperties props;
    props.setStorageParameters(userSettingsTestOptions());
    if (auto* settings = props.getUserSettings()) {
        settings->setValue(key, value);
        settings->saveIfNeeded();
    }
}

void removeRealKey(const juce::String& key) {
    juce::ApplicationProperties props;
    props.setStorageParameters(userSettingsTestOptions());
    if (auto* settings = props.getUserSettings()) {
        settings->removeValue(key);
        settings->saveIfNeeded();
    }
}

bool realKeyExists(const juce::String& key) {
    juce::ApplicationProperties props;
    props.setStorageParameters(userSettingsTestOptions());
    auto* settings = props.getUserSettings();
    return settings != nullptr && settings->containsKey(key);
}

juce::String readRealKey(const juce::String& key) {
    juce::ApplicationProperties props;
    props.setStorageParameters(userSettingsTestOptions());
    auto* settings = props.getUserSettings();
    return settings != nullptr ? settings->getValue(key, {}) : juce::String();
}
} // namespace

TEST(DetachRedockStateTests, MixerAndTimelineWindowBoundsKeysAreRestoredAfterAGuardedDetachTest) {
    // Captures whatever this MACHINE'S real pre-test values are (constructed before anything below
    // writes a single byte) and restores them when this test ends, however it ends -- the sentinel
    // setup two lines down is itself a real-file mutation, so it needs the SAME protection every
    // other real-settings-file test here gets, not just the inner guard under test.
    PersistedKeysGuard outerGuard({"mixerWindowBounds", "timelineWindowBounds"});

    const juce::String sentinelMixerBounds = "10 20 700 500";
    writeRealKey("mixerWindowBounds", sentinelMixerBounds); // stands in for "the developer's own value"
    removeRealKey("timelineWindowBounds");                  // stands in for "the key never existed"

    {
        PersistedKeysGuard guard({"mixerWindowBounds", "timelineWindowBounds"});

        MainComponent mc(std::make_unique<MockProviderDRST>());
        mc.setSize(1400, 900);
        mc.newPatchForTest();

        // The exact production path that leaked these two keys before FRO101/this guard: detach
        // both real panels against a real MainComponent, which persistBounds()'s each into the
        // real settings file.
        auto& mixerHost = mc.getMixerDock().getMixerHost();
        mixerHost.setDetached(true);
        mc.getMixerDock().getTimelineHost().setDetached(true);
        // The sentinel is itself a plausible rect, so restoreBoundsOrDefault() reads it straight
        // back and persistBounds() re-persists that SAME string -- not a discriminating check on
        // its own. Force an actual interactive-style resize (moved()+resized() -> persistBounds())
        // so the key is guaranteed to hold something OTHER than the sentinel before the guard has
        // to restore it.
        ASSERT_NE(mixerHost.getDetachedWindowForTest(), nullptr);
        mixerHost.getDetachedWindowForTest()->setBounds(111, 222, 640, 480);
        ASSERT_NE(readRealKey("mixerWindowBounds"), sentinelMixerBounds)
            << "sanity check -- the resize above must actually have written a NEW value over the "
               "sentinel, or this test would pass even with no guard at all";
        ASSERT_TRUE(realKeyExists("timelineWindowBounds"))
            << "sanity check -- detaching must actually have created this key";
    } // guard destructs here -- must put both keys back exactly as they were above

    EXPECT_EQ(readRealKey("mixerWindowBounds"), sentinelMixerBounds)
        << "a key that had a real value must be restored to EXACTLY that value, not cleared";
    EXPECT_FALSE(realKeyExists("timelineWindowBounds"))
        << "a key that did not exist before the test must go back to not existing, not \"\"";
}
