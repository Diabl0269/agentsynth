#pragma once

// Shared test infrastructure for FocusArbitration*Tests.cpp: the persisted-settings guards, the
// headless AI provider stub, and the FocusArbitrationTest fixture itself. See
// FocusArbitrationClipboardTests.cpp for the one-focus-ownership-rule background comment.
#include "AI/AIProvider.h"
#include "AI/AIStateMapper/AIStateMapper.h"
#include "AudioEngine/AudioEngine.h"
#include "MainComponent/MainComponent.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "UI/Settings/PreferencesSettingsTab/PreferencesSettingsTab.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Theme/ThemeManager.h"
#include <algorithm>
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>
#include <utility>
#include <vector>

namespace {

// automateParameter()'s toggle path (and, in principle, any test that ever called
// simulateToggleTimelineClick()) persists "timelinePanelVisible" to the SAME on-disk properties
// file every MainComponent instance reads at construction — reset it before AND after every test
// in this file so SurfaceResolverRealFocus's "the panel starts hidden" precondition never depends
// on what ran before it in the same process. Mirrors AutomationEditorTests.cpp's helper of the
// same name exactly.
void resetTimelinePanelVisibleKey() {
    juce::PropertiesFile::Options opts;
    opts.applicationName = "Agent Synth";
    opts.folderName = "Agent Synth";
    opts.filenameSuffix = "settings";
    opts.osxLibrarySubFolder = "Application Support";
    opts.storageFormat = juce::PropertiesFile::storeAsXML;

    juce::ApplicationProperties props;
    props.setStorageParameters(opts);
    if (auto* s = props.getUserSettings()) {
        s->setValue("timelinePanelVisible", "0");
        s->saveIfNeeded();
    }
}

// The one on-disk settings file every MainComponent in this process opens (see
// synth::userSettingsOptions) — factored out of resetTimelinePanelVisibleKey so the guard below can
// reach the same file.
juce::PropertiesFile::Options userSettingsTestOptions() {
    juce::PropertiesFile::Options opts;
    opts.applicationName = "Agent Synth";
    opts.folderName = "Agent Synth";
    opts.filenameSuffix = "settings";
    opts.osxLibrarySubFolder = "Application Support";
    opts.storageFormat = juce::PropertiesFile::storeAsXML;
    return opts;
}

// Removes "timelineSnap"/"timelineSnapEnabled" so the NEXT MainComponent constructed in this process
// restores TimelinePanelComponent's documented default (Snap::Quarter, enabled) instead of whatever
// division a developer's own timeline is currently set to — see restoreViewPreferences(), which falls
// back to its own already-default-initialised viewState_ only when the key is absent, so removing
// (not zeroing) the keys is what makes that fallback kick in. Callers still need a PersistedKeysGuard
// around this to put the developer's real values back afterward.
void resetTimelineSnapKeysToDefault() {
    juce::ApplicationProperties props;
    props.setStorageParameters(userSettingsTestOptions());
    if (auto* s = props.getUserSettings()) {
        s->removeValue("timelineSnap");
        s->removeValue("timelineSnapEnabled");
        s->saveIfNeeded();
    }
}

// Saves the named settings keys on construction and restores them EXACTLY on destruction, including
// the case where a key did not exist at all.
//
// Needed because the commands under test persist as a side effect: setSnapValue() writes
// "timelineSnap"/"timelineSnapEnabled", and the Preferences toggle writes "naturalScrolling" — all
// into the REAL user settings file this machine's app reads. Clearing them afterwards would not be
// enough: it would silently change the developer's own preferences, so the original values go back.
//
// Both the read and the write use their own short-lived juce::ApplicationProperties, exactly as
// resetTimelinePanelVisibleKey does: a PropertiesFile saves its WHOLE in-memory property set, so a
// long-lived instance held across the test would write back a snapshot taken before the test and
// clobber every unrelated key the test happened to touch.
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

// A provider that never touches the network — this file only exercises the graph/timeline/command
// plumbing, never the AI chat itself. Mirrors NullAIProvider (PluginProcessorTests.cpp) exactly.
class FocusMockProvider : public synth::AIProvider {
public:
    juce::String getProviderName() const override { return "FocusMock"; }
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

// Pins the one piece of persisted, machine-dependent state the tests below would otherwise inherit.
// TimelinePanelComponent restores snapEnabled/snap from the REAL user settings file at construction
// (restoreViewPreferences -> "timelineSnap"/"timelineSnapEnabled"), which is exactly why
// CopyPasteClipsRebasedAtPlayhead above is red on a machine whose owner turned snap off and green in
// CI. Every test added after it therefore pins the shared view state AFTER construction, and writes
// the fields DIRECTLY rather than calling setSnapEnabled() — that setter persists, which would make
// one test's arrangement leak into the next run's defaults.
void pinSnapOff(MainComponent& mc) {
    auto& view = mc.getTimelinePanel().getViewState();
    view.snap = synth::ui::TimelineViewState::Snap::Off;
    view.snapEnabled = false;
}

// True when getCommandInfo reports `cmdId` as enabled for whatever surface `mc` currently resolves
// to. juce::ApplicationCommandInfo carries "disabled" rather than "active", so every call site would
// otherwise repeat the same flag-mask double negative.
bool commandIsActive(MainComponent& mc, juce::CommandID cmdId) {
    juce::ApplicationCommandInfo info(cmdId);
    mc.getCommandInfo(cmdId, info);
    return (info.flags & juce::ApplicationCommandInfo::isDisabled) == 0;
}

} // namespace

class FocusArbitrationTest : public ::testing::Test {
protected:
    void SetUp() override { resetTimelinePanelVisibleKey(); }
    void TearDown() override { resetTimelinePanelVisibleKey(); }
};
