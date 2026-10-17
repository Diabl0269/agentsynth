#pragma once

// BottomDockMainAreaFixture.h -- a real off-screen MainComponent for the tests of a view in the main area
// (BottomDockMainAreaTests.cpp, BottomDockMixerMainTests.cpp).
#include "../../App/MainComponent/MainComponentTestFixture.h"
#include "../../TestSettingsHelpers.h"
#include "BottomDockActiveTabResetGuard.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Layout/BottomDockComponent/BottomDockComponent.h"
#include "UI/Timeline/TimelinePanelComponent/TimelinePanelComponent.h"
#include <gtest/gtest.h>
#include <memory>
#include <optional>

class BottomDockMainAreaTest : public MainComponentTest {
protected:
    void SetUp() override {
        MainComponentTest::SetUp();
        resetGuard_.emplace();
        build();
    }
    void TearDown() override {
        mc_.reset();
        resetGuard_.reset();
        MainComponentTest::TearDown();
    }
    void build() {
        mc_ = std::make_unique<MainComponent>(std::make_unique<MockProvider>());
        mc_->setSize(1400, 900);
        mc_->newPatchForTest();
    }

    synth::ui::BottomDockComponent& dock() { return mc_->getBottomDock(); }
    juce::Component& holder() { return mc_->getMainViewHolderForTest(); }
    synth::ui::TimelinePanelComponent& timeline() { return mc_->getTimelinePanel(); }
    bool regionOpen(const char* id) {
        const auto* region = mc_->getFocusRegionsForTest().findById(id);
        return region != nullptr && region->isCurrentlyOpen();
    }
    juce::Rectangle<int> regionBounds(const char* id) {
        return mc_->getFocusRegionsForTest().findById(id)->root->getBounds();
    }
    bool commandActive(juce::CommandID id) {
        juce::ApplicationCommandInfo info(id);
        mc_->getCommandInfo(id, info);
        return (info.flags & juce::ApplicationCommandInfo::isDisabled) == 0;
    }

    std::optional<BottomDockActiveTabResetGuardMDT> resetGuard_;
    std::unique_ptr<MainComponent> mc_;
};
