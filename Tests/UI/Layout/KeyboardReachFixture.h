#pragma once

// KeyboardReachFixture.h -- the fixtures of the keyboard walks over every focus region and every dialog
// (KeyboardReachTests.cpp: every control is a stop; TabOrderReadingSweepTests.cpp: Tab follows reading order).
// A real MainComponent with a track on the timeline and the mixer built, and a dialog fixture that keeps native
// windows and popup motion out of the way.
#include "../../App/MainComponent/MainComponentTestFixture.h"
#include "BottomDockActiveTabResetGuard.h"
#include "NativeWindowTestGuard.h"
#include "UI/Layout/BottomDockComponent/BottomDockComponent.h"
#include "UI/Layout/FadeVisibility.h"
#include "UI/Layout/PopupMotion.h"
#include <gtest/gtest.h>
#include <memory>
#include <optional>

class KeyboardReachFixture : public MainComponentTest {
protected:
    void SetUp() override {
        MainComponentTest::SetUp();
        resetGuard_.emplace();
        mc_ = std::make_unique<MainComponent>(std::make_unique<MockProvider>());
        mc_->setSize(1400, 900);
        mc_->newPatchForTest();
        synth::ui::FadeVisibility::setAnimateOffScreenForTest(false); // a hidden pane is hidden at once
        mc_->simulateAddAudioTrackClick();
        mc_->getTimelineDoc().addTrack(synth::TrackKind::Midi, "Track 1");
        rebuildMixer();
        mc_->resized();
    }
    void TearDown() override {
        mc_.reset();
        resetGuard_.reset();
        MainComponentTest::TearDown();
    }

    // Builds the mixer columns (defined in KeyboardReachTests.cpp, which already reaches the mixer panel).
    void rebuildMixer();

    // Every region of the registry as it stands, opened first when it is closed and can be opened.
    std::vector<synth::ui::FocusRegion> openRegions() {
        std::vector<synth::ui::FocusRegion> regions;
        for (const auto& region : mc_->getFocusRegionsForTest().getRegions()) {
            if (!region.isCurrentlyOpen() && region.open)
                region.open();
            mc_->resized();
        }
        for (const auto& region : mc_->getFocusRegionsForTest().getRegions())
            if (region.isCurrentlyOpen() && region.root != nullptr)
                regions.push_back(region);
        return regions;
    }

    std::optional<BottomDockActiveTabResetGuardMDT> resetGuard_;
    std::unique_ptr<MainComponent> mc_;
};

class KeyboardReachDialogFixture : public ::testing::Test {
protected:
    void SetUp() override {
        if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr)
            GTEST_SKIP() << "needs a display";
        if (nativeWindowsAbortHere())
            GTEST_SKIP() << "native windows abort the process under the ASAN job's Xvfb";
        juce::MessageManager::getInstance();
        wasEnabled_ = synth::ui::PopupMotion::isEnabled();
        synth::ui::PopupMotion::setEnabled(false);
    }
    void TearDown() override {
        synth::ui::deleteOpenModalDialogs();
        synth::ui::PopupMotion::setEnabled(wasEnabled_);
    }
    bool wasEnabled_ = true;
};
