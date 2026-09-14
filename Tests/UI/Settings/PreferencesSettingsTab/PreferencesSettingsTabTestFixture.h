#pragma once

// Shared fixture + component-tree helpers for the PreferencesSettingsTab*Tests.cpp split.

#include "AI/AIStateMapper/AIStateMapper.h"
#include "Modules/ModuleBase.h"
#include "ShortcutManager/ShortcutManager.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Settings/PreferencesSettingsTab/PreferencesSettingsTab.h"
#include <algorithm>
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

class PreferencesSettingsTabTest : public ::testing::Test {
protected:
    void SetUp() override {
        juce::PropertiesFile::Options options;
        options.applicationName = "PreferencesTabTest";
        options.filenameSuffix = "test";
        options.storageFormat = juce::PropertiesFile::storeAsXML;
        appProperties.setStorageParameters(options);
        if (auto* s = appProperties.getUserSettings())
            s->clear();
    }

    void TearDown() override {
        if (auto* s = appProperties.getUserSettings())
            s->clear();
    }

    juce::ApplicationProperties appProperties;
};

// T157: the preference groups now live inside the tab's scroll view - a juce::Viewport whose owned
// component (a ContentHost) holds the rows - so a control is no longer a DIRECT child of the tab.
// These tests walk the whole component tree, mirroring how the Keyboard Shortcuts tab's rows live
// inside their own scrolled host.
namespace {
std::vector<juce::Component*> descendantsOf(juce::Component& root) {
    std::vector<juce::Component*> out;
    std::vector<juce::Component*> stack;
    for (auto* c : root.getChildren())
        stack.push_back(c);
    while (!stack.empty()) {
        juce::Component* c = stack.back();
        stack.pop_back();
        out.push_back(c);
        for (auto* ch : c->getChildren())
            stack.push_back(ch);
    }
    return out;
}
} // namespace

namespace {
juce::ToggleButton* findToggleByText(PreferencesSettingsTab& tab, const juce::String& text) {
    for (auto* child : descendantsOf(tab))
        if (auto* tb = dynamic_cast<juce::ToggleButton*>(child))
            if (tb->getButtonText().containsIgnoreCase(text))
                return tb;
    return nullptr;
}
} // namespace
