#pragma once

// TimelinePanelTestFixture.h
//
// Shared fixtures for the TimelinePanel test suite (Tests/UI/Timeline/TimelinePanel/TimelinePanel*Tests.cpp).
// Header-only; not compiled on its own and not registered in Tests/CMakeLists.txt.

#include "AI/AIProvider.h"
#include "AudioEngine/AudioEngine.h"
#include "MainComponent/MainComponent.h"
#include "TimelinePanelTestEvents.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

// ============================================================================
// Mock AI provider — same minimal pattern as MainComponentTests.cpp's MockProvider.
// ============================================================================
class MockProviderTL : public synth::AIProvider {
public:
    juce::String getProviderName() const override { return "MockTL"; }
    void fetchAvailableModels(std::function<void(const juce::StringArray&, bool)> callback) override {
        callback({"MockModel"}, true);
    }
    RequestId sendPrompt(const std::vector<Message>&, CompletionCallback callback, const juce::var&,
                         std::function<void(const juce::String&)> = {}) override {
        AIResponse response;
        response.success = true;
        response.content = "Mock response.";
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

// FRO11 (P9-5): mc.getTimelinePanel() is no longer a direct child of MainComponent -- it now
// lives inside BottomDockComponent (the Timeline/Mixer tab strip), inset by the tab strip's own
// height. getBounds() therefore returns coordinates relative to the DOCK, not MainComponent, so a
// raw `mc.getTimelinePanel().getBounds()` can no longer be compared directly against another
// MainComponent-level rect (the status bar, the graph editor...) -- that comparison silently
// mixed two different coordinate origins before this helper existed. Converts through
// MainComponent's own coordinate space via Component::getLocalArea(), the standard JUCE idiom for
// "this component's bounds as seen from an ancestor".
inline juce::Rectangle<int> timelinePanelBoundsInMainComponent(MainComponent& mc) {
    auto& panel = mc.getTimelinePanel();
    return mc.getLocalArea(&panel, panel.getLocalBounds());
}

// FRO11 (P9-5): "is the timeline panel actually open and on screen" now takes both of
// timelinePanel's own isVisible() (true only when the Timeline tab is selected) AND
// bottomDock's isVisible() (true only when the dock itself is open) -- composing the two LOCAL
// flags, not juce::Component::isShowing(), because isShowing() additionally requires the ROOT
// component to have a real Desktop peer (its own implementation walks up to the top-level
// component and checks getPeer()), which is never true in a headless test: this codebase's own
// MainComponent tests never call addToDesktop() (see PanelAnimationAndLoadingTests.cpp's own
// "test premise: headless, no real window" comment). isVisible() at each level needs no peer, so
// it composes correctly in both headless tests and the real app.
inline bool timelinePanelIsOpen(MainComponent& mc) {
    return mc.getTimelinePanel().isVisible() && mc.getBottomDock().isVisible();
}

class TimelinePanelIntegrationTest : public ::testing::Test {
protected:
    // Same pattern as MainComponentTests.cpp / PanelAnimationAndLoadingTests.cpp: the delegating
    // MainComponent ctor reads/writes the shared on-disk "Agent Synth" ApplicationProperties, so
    // panel-visibility keys are reset to their documented defaults before AND after every test to
    // keep persistence tests hermetic regardless of execution order.
    void resetPanelKeys() {
        juce::PropertiesFile::Options opts;
        opts.applicationName = "Agent Synth";
        opts.folderName = "Agent Synth";
        opts.filenameSuffix = "settings";
        opts.osxLibrarySubFolder = "Application Support";
        opts.storageFormat = juce::PropertiesFile::storeAsXML;

        juce::ApplicationProperties props;
        props.setStorageParameters(opts);
        if (auto* s = props.getUserSettings()) {
            s->setValue("librarySidebarVisible", "1"); // default: visible
            s->setValue("aiPanelVisible", "0");        // default: hidden
            s->setValue("minimapVisible", "1");        // default: visible
            s->setValue("bottomDockVisible", "0");     // default: hidden
            // Removed, not defaulted: absent is what makes the theme metric the default height.
            s->removeValue(MainComponent::kTimelinePanelHeightKey);
            s->saveIfNeeded();
        }
    }

    void SetUp() override { resetPanelKeys(); }
    void TearDown() override { resetPanelKeys(); }
};

// MainComponent owns the doc, the recorder and the reconcile hooks.
class TimelineAppWiringTest : public TimelinePanelIntegrationTest {
protected:
    // An empty canvas plus `polyMidiCount` Poly MIDI modules, and a clean undo stack, so a test's
    // first undo step is the one it just took.
    static void prepareCanvas(MainComponent& mc, int polyMidiCount) {
        mc.getGraphEditor().newPatch();
        for (int i = 0; i < polyMidiCount; ++i)
            mc.getGraphEditor().addModuleAtCanvasPosition("Poly MIDI", {200 + i * 320, 200}, {});
        mc.getUndoManager().clearUndoHistory();
    }

    // Nothing in these tests wants a real device clocking the graph underneath them: the transport
    // is ticked by hand, and the timeline snapshot exchange is read from this thread.
    static void quiesceEngine(MainComponent& mc) { mc.getAudioEngine().suspendDeviceCallback(); }
};
