// PanelStepKeysTests.cpp -- the AI panel and the MIDI Remote panel are plain-container focus regions: Up and Down step
// through their controls and Escape returns to the panel (UI/Layout/FocusStepWithin.h). Keys are delivered the way the
// window does (PaneKeysWalk.h); a headless window cannot take focus, so the step is read from PaneKeys.
#include "../Assistant/AIChatComponent/AIChatComponentTestFixture.h"
#include "../MidiRemote/MidiRemotePanelTestFixture.h"
#include "PaneKeysWalk.h"
#include "UI/Layout/FocusStepWithin.h"
#include "UI/MidiRemote/ControllerSurface/ControllerSurfaceComponent.h"
#include <functional>
#include <gtest/gtest.h>

namespace {

using pane_walk::dispatchKey;
using pane_walk::plainKey;

} // namespace

// Regression test for FRO1816: the AI panel's controls had no Up/Down stepping, so none of them was reachable.
TEST_F(AIChatComponentTest, UpAndDownStepThroughTheAIPanelControlsAndEscapeReturnsToThePanel) {
    AudioEngine engine;
    synth::AIIntegrationService service(engine.getGraph());
    service.setProvider(std::make_unique<MockChatProvider>());
    juce::ApplicationProperties props;
    juce::PropertiesFile::Options options;
    options.applicationName = "PanelStepKeysTest";
    options.filenameSuffix = "test";
    options.storageFormat = juce::PropertiesFile::storeAsXML;
    props.setStorageParameters(options);

    synth::AIChatComponent chat(service, props);
    chat.setSize(400, 600);
    auto& keys = chat.getPaneKeysForTest();

    juce::Component* editor = nullptr;
    for (auto* stop : synth::ui::focusableDescendants(chat))
        if (dynamic_cast<juce::TextEditor*>(stop) != nullptr && stop->getTitle() == "Message to the AI assistant")
            editor = stop;
    ASSERT_NE(editor, nullptr);
    EXPECT_FALSE(keys.isAttachedTo(*editor)) << "the message box has its own listener (Escape cancels a request)";
    EXPECT_TRUE(dispatchKey(keys, chat, *editor, plainKey(juce::KeyPress::downKey)));
    EXPECT_TRUE(dispatchKey(keys, chat, *editor, plainKey(juce::KeyPress::upKey)));

    // Every control but the message box: Down, Up and Escape land where the on-screen order says.
    std::vector<juce::Component*> stops;
    for (auto* stop : synth::ui::focusableDescendants(chat))
        if (stop != editor)
            stops.push_back(stop);
    for (const char* name : {"AI model", "History", "New Chat", "Send"}) {
        bool found = false;
        for (auto* stop : stops)
            found = found || stop->getTitle() == name ||
                    (dynamic_cast<juce::Button*>(stop) != nullptr &&
                     static_cast<juce::Button*>(stop)->getButtonText() == name);
        EXPECT_TRUE(found) << name << " is a stop of the panel";
    }
    for (auto* stop : stops) {
        SCOPED_TRACE(stop->getTitle());
        EXPECT_TRUE(keys.isAttachedTo(*stop));
        EXPECT_TRUE(dispatchKey(keys, chat, *stop, plainKey(juce::KeyPress::downKey)));
        EXPECT_EQ(keys.getLastStepForTest(), synth::ui::neighbourStop(chat, stop, +1));
        EXPECT_TRUE(dispatchKey(keys, chat, *stop, plainKey(juce::KeyPress::upKey)));
        EXPECT_EQ(keys.getLastStepForTest(), synth::ui::neighbourStop(chat, stop, -1) != nullptr
                                                 ? synth::ui::neighbourStop(chat, stop, -1)
                                                 : &chat);
        EXPECT_TRUE(dispatchKey(keys, chat, *stop, plainKey(juce::KeyPress::escapeKey)));
    }

    // From the panel itself Down enters the first control and Up has nowhere to go; neither key leaves the panel.
    EXPECT_EQ(synth::ui::neighbourStop(chat, nullptr, +1), synth::ui::focusableDescendants(chat).front());
    EXPECT_TRUE(chat.keyPressed(plainKey(juce::KeyPress::downKey)));
    EXPECT_TRUE(chat.keyPressed(plainKey(juce::KeyPress::upKey)));
    // Escape at the panel keeps its meaning for the window (closing the panel).
    EXPECT_FALSE(chat.keyPressed(plainKey(juce::KeyPress::escapeKey)));
}

// Regression test for FRO1816: the "Upgrade to Pro" strip and a reply's Upgrade button show later than the first
// layout; the keys they ignore bubble to the panel, which steps from where focus is.
TEST_F(AIChatComponentTest, UpgradeToProIsAStopOfTheAIPanelOnceShown) {
    AudioEngine engine;
    synth::AIIntegrationService service(engine.getGraph());
    service.setProvider(std::make_unique<MockChatProvider>());
    juce::ApplicationProperties props;
    juce::PropertiesFile::Options options;
    options.applicationName = "PanelStepKeysTest";
    options.filenameSuffix = "test";
    options.storageFormat = juce::PropertiesFile::storeAsXML;
    props.setStorageParameters(options);
    synth::AIChatComponent chat(service, props);
    chat.setSize(400, 600);
    auto* upsell = findDescendantWithText<juce::TextButton>(&chat, "Upgrade to Pro");
    ASSERT_NE(upsell, nullptr);
    EXPECT_TRUE(chat.getPaneKeysForTest().isAttachedTo(*upsell));
    EXPECT_TRUE(upsell->getWantsKeyboardFocus());
}

// Regression test for FRO1816: the MIDI Remote panel's Add controller and More buttons and its controller surface
// were not reached by keys.
TEST_F(MidiRemotePanelLiveRefreshTest, UpAndDownStepThroughTheMidiRemotePanelControls) {
    panel_.setSize(1400, 700);
    panel_.resized();
    auto& keys = panel_.getPaneKeysForTest();

    juce::Component* surface = nullptr;
    for (auto* stop : synth::ui::focusableDescendants(panel_))
        if (dynamic_cast<synth::ui::ControllerSurfaceComponent*>(stop) != nullptr)
            surface = stop;
    ASSERT_NE(surface, nullptr) << "the controller surface takes keyboard focus";
    EXPECT_EQ(surface->getTitle(), "Controller surface");
    EXPECT_FALSE(keys.isAttachedTo(*surface)) << "the surface keeps Escape for clearing its selection";

    std::vector<juce::Component*> stops;
    for (auto* stop : synth::ui::shownFocusableDescendants(panel_))
        if (stop != surface)
            stops.push_back(stop);
    for (const char* id : {"addControllerButton", "moreButton"}) {
        juce::Component* button = nullptr;
        std::function<void(juce::Component&)> find = [&](juce::Component& parent) {
            for (auto* child : parent.getChildren()) {
                if (child->getComponentID() == id)
                    button = child;
                find(*child);
            }
        };
        find(panel_);
        ASSERT_NE(button, nullptr) << id;
        EXPECT_TRUE(button->getWantsKeyboardFocus() && keys.isAttachedTo(*button)) << id << " is a stop of the panel";
    }
    for (auto* stop : stops) {
        SCOPED_TRACE(stop->getTitle() + " / " + stop->getComponentID());
        EXPECT_TRUE(keys.isAttachedTo(*stop));
        EXPECT_TRUE(dispatchKey(keys, panel_, *stop, plainKey(juce::KeyPress::downKey)));
        EXPECT_EQ(keys.getLastStepForTest(), synth::ui::neighbourStop(panel_, stop, +1));
        EXPECT_TRUE(dispatchKey(keys, panel_, *stop, plainKey(juce::KeyPress::upKey)));
        EXPECT_TRUE(dispatchKey(keys, panel_, *stop, plainKey(juce::KeyPress::escapeKey)));
    }

    // From the surface, whose keys the panel sees by bubbling: Up and Down are used, Escape with nothing selected is
    // left for the panel to take back to its root.
    EXPECT_TRUE(dispatchKey(keys, panel_, *surface, plainKey(juce::KeyPress::downKey)));
    EXPECT_TRUE(dispatchKey(keys, panel_, *surface, plainKey(juce::KeyPress::upKey)));
}
