// DialogTooltipTests.cpp (docs/layout/animation.md#tooltips): every dialog PopupMotion::launchDialog opens is a native
// window of its own, so it hosts its own AppTooltipWindow; the main window's would draw its tips under the dialog.
// Dialogs come from the theme sweep's registry, so a new dialog is covered the day it is registered.
#include "../Theme/ThemeSweep/ThemeSweepSurfaces.h"
#include "NativeWindowTestGuard.h"
#include "UI/Layout/AppTooltipWindow.h"
#include "UI/Layout/DialogTooltip.h"
#include "UI/Layout/HelperTooltip.h"
#include "UI/Layout/PopupMotion.h"
#include "UI/Theme/BuiltInThemes.h"
#include <gtest/gtest.h>

using namespace synth::test::theme_sweep;
using synth::ui::AppTooltipWindow;
using synth::ui::PopupMotion;
namespace dt = synth::ui::dialog_tooltip;

namespace {

// juce gives no hover tip outside the foreground app, and a CI test process (Windows runner, headless Linux) never is.
// A hidden automation process counts as foreground, so this lets the real tip path run on every platform.
struct TipsCountAsForeground {
    TipsCountAsForeground() { juce::ComponentPeer::hideWindowsForAutomation = true; }
    ~TipsCountAsForeground() { juce::ComponentPeer::hideWindowsForAutomation = previous; }
    const bool previous = juce::ComponentPeer::hideWindowsForAutomation;
};

class DialogTooltip : public ::testing::Test {
protected:
    void SetUp() override {
        if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr)
            GTEST_SKIP() << "needs a display";
        if (nativeWindowsAbortHere())
            GTEST_SKIP() << "native windows abort the process under the ASAN job's Xvfb";
        juce::MessageManager::getInstance();
        wasEnabled_ = PopupMotion::isEnabled();
        PopupMotion::setEnabled(false);
    }
    void TearDown() override {
        synth::ui::deleteOpenModalDialogs();
        dt::setAppProperties(nullptr); // a test seam must not leak into the next test
        PopupMotion::setEnabled(wasEnabled_);
    }
    bool wasEnabled_ = true;
};

// A dialog with one button that has a tooltip, opened through launchDialog.
struct Hosted {
    Hosted() {
        auto content = std::make_unique<juce::Component>();
        content->setSize(200, 100);
        button.setTooltip("Hover tip");
        button.setBounds(10, 10, 80, 24);
        content->addAndMakeVisible(button);
        juce::DialogWindow::LaunchOptions options;
        options.dialogTitle = "Tooltip host";
        options.content.setOwned(content.release());
        options.useNativeTitleBar = false;
        dialog = PopupMotion::launchDialog(options);
    }
    juce::TextButton button{"Go"};
    juce::DialogWindow* dialog = nullptr;
};

} // namespace

TEST_F(DialogTooltip, ALaunchedDialogHostsExactlyOneTooltipWindowThatReturnsTheTip) {
    Hosted host;
    ASSERT_NE(host.dialog, nullptr);
    EXPECT_EQ(dt::countIn(*host.dialog), 1);
    auto* tip = dt::tooltipOf(*host.dialog);
    ASSERT_NE(tip, nullptr);
    // The button lives in the dialog's content, so the dialog's own tooltip window serves it.
    TipsCountAsForeground foreground;
    EXPECT_EQ(tip->getTipFor(*host.dialog->getContentComponent()->getChildComponent(0)), "Hover tip");
}

TEST_F(DialogTooltip, TheTooltipWindowReadsShowInfoTooltipsFromTheAppProperties) {
    juce::ApplicationProperties properties;
    juce::PropertiesFile::Options options;
    options.applicationName = "DialogTooltipTest";
    options.filenameSuffix = "test";
    properties.setStorageParameters(options);
    properties.getUserSettings()->clear();
    properties.getUserSettings()->setValue(synth::ui::kShowInfoTooltipsKey, false);
    dt::setAppProperties(&properties);
    {
        Hosted host;
        ASSERT_NE(host.dialog, nullptr);
        auto* tip = dt::tooltipOf(*host.dialog);
        ASSERT_NE(tip, nullptr);
        TipsCountAsForeground foreground;
        EXPECT_EQ(tip->getTipFor(host.button), "") << "an info tip is hidden while the preference is off";
        synth::ui::markHelperTooltip(host.button);
        EXPECT_EQ(tip->getTipFor(host.button), "Hover tip") << "a helper tip still shows";
        synth::ui::deleteOpenModalDialogs();
    }
    dt::setAppProperties(nullptr);
    properties.getUserSettings()->clear();
}

TEST_F(DialogTooltip, ClosingADialogTakesItsTooltipWindowWithIt) {
    Hosted host;
    ASSERT_NE(host.dialog, nullptr);
    EXPECT_EQ(PopupMotion::openDialogsForTest().size(), 1u);
    synth::ui::deleteOpenModalDialogs(); // would crash or leak (leak detector) if the tip outlived its window
    EXPECT_TRUE(PopupMotion::openDialogsForTest().empty());
}

TEST_F(DialogTooltip, EveryRegisteredDialogHostsOneTooltipWindow) {
    int opened = 0;
    for (const auto& surface : dialogSurfaces()) {
        if (!surface.open)
            continue;
        SweepEnvironment env(synth::theme::makeDaylight());
        auto* dialog = surface.open(env);
        ASSERT_NE(dialog, nullptr) << surface.name;
        EXPECT_EQ(dt::countIn(*dialog), 1) << surface.name << " has no tooltip window of its own";
        ++opened;
    }
    EXPECT_GT(opened, 0);
}
