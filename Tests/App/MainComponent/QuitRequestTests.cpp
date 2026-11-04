// Concern: the app's answer to a system quit request (the Quit menu item, Cmd+Q) while a dialog that
// PopupMotion::launchDialog opened is up, for every dialog of the theme sweep's registry. The native menu step is not
// in reach of a unit test; everything after it is: the unsaved-changes question is offered, Discard quits, and
// deleteOpenModalDialogs() (what shutdown runs) leaves no modal window behind.
#include "../../TestWait.h"
#include "../../UI/Layout/NativeWindowTestGuard.h"
#include "../../UI/Theme/ThemeSweep/ThemeSweepSurfaces.h"
#include "AudioEngine/AudioEngine.h"
#include "MainComponent/QuitRequest.h"
#include "MainComponentTestFixture.h"
#include "UI/Layout/PopupMotion.h"
#include "UI/Theme/BuiltInThemes.h"

using namespace synth::test::theme_sweep;
using synth::ui::PopupMotion;

namespace {

void pump(int ms = 50) { juce::MessageManager::getInstance()->runDispatchLoopUntil(ms); }

class QuitWithDialog : public MainComponentTest {
protected:
    void SetUp() override {
        MainComponentTest::SetUp();
        if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr)
            GTEST_SKIP() << "needs a display";
        if (nativeWindowsAbortHere())
            GTEST_SKIP() << "native windows abort the process under the ASAN job's Xvfb";
        wasEnabled_ = PopupMotion::isEnabled();
        PopupMotion::setEnabled(false); // a dialog shows at once
    }
    void TearDown() override {
        synth::ui::deleteOpenModalDialogs();
        PopupMotion::setEnabled(wasEnabled_);
        MainComponentTest::TearDown();
    }
    bool wasEnabled_ = true;
};

struct Prompt {
    int calls = 0;
    juce::String label;
    std::function<void(MainComponent::UnsavedChangesChoice)> answer;
    void installOn(MainComponent& mc) {
        mc.unsavedChangesPrompt = [this](const juce::String& l,
                                         std::function<void(MainComponent::UnsavedChangesChoice)> c) {
            ++calls;
            label = l;
            answer = std::move(c);
        };
    }
};

void makeDirty(MainComponent& mc) {
    mc.simulateAddMidiTrackClick();
    for (int i = 0; i < 40 && !mc.isProjectDirty(); ++i) // the flag follows the undo manager's async broadcast
        pump(25);
}

} // namespace

TEST_F(QuitWithDialog, ACleanProjectQuitsAtOnceWithEveryDialogOpen) {
    for (const auto& surface : dialogSurfaces()) {
        if (!surface.open)
            continue;
        MainComponent mc(std::make_unique<MockProvider>());
        mc.setSize(1600, 900);
        mc.getAudioEngine().suspendDeviceCallback();
        Prompt prompt;
        prompt.installOn(mc);
        SweepEnvironment env(synth::theme::makeDaylight());
        ASSERT_NE(surface.open(env), nullptr) << surface.name;

        int quits = 0;
        synth::requestQuit(&mc, [&quits] { ++quits; });

        EXPECT_EQ(quits, 1) << surface.name;
        EXPECT_EQ(prompt.calls, 0) << surface.name << ": nothing to save, nothing to ask";
    }
}

TEST_F(QuitWithDialog, ADirtyProjectIsAskedAboutAndDiscardQuitsWithEveryDialogOpen) {
    for (const auto& surface : dialogSurfaces()) {
        if (!surface.open)
            continue;
        MainComponent mc(std::make_unique<MockProvider>());
        mc.setSize(1600, 900);
        mc.getAudioEngine().suspendDeviceCallback();
        Prompt prompt;
        prompt.installOn(mc);
        makeDirty(mc);
        ASSERT_TRUE(mc.isProjectDirty()) << surface.name;
        SweepEnvironment env(synth::theme::makeDaylight());
        ASSERT_NE(surface.open(env), nullptr) << surface.name;

        int quits = 0;
        synth::requestQuit(&mc, [&quits] { ++quits; });

        ASSERT_EQ(prompt.calls, 1) << surface.name << ": the unsaved-changes question is offered";
        EXPECT_EQ(prompt.label, "Quitting") << surface.name;
        EXPECT_EQ(quits, 0) << surface.name << ": not before the answer";
        prompt.answer(MainComponent::UnsavedChangesChoice::Cancel);
        EXPECT_EQ(quits, 0) << surface.name << ": Cancel keeps the app";
        synth::requestQuit(&mc, [&quits] { ++quits; });
        prompt.answer(MainComponent::UnsavedChangesChoice::Discard);
        EXPECT_EQ(quits, 1) << surface.name << ": Discard quits";

        // What shutdown() does next: no dialog may outlive the app objects.
        synth::ui::deleteOpenModalDialogs();
        EXPECT_EQ(juce::ModalComponentManager::getInstance()->getNumModalComponents(), 0) << surface.name;
        EXPECT_TRUE(PopupMotion::openDialogsForTest().empty()) << surface.name;
    }
}

// The real question (no test seam) must show above the dialog that is open, take the click and quit.
TEST_F(QuitWithDialog, TheRealQuestionShowsAboveTheOpenDialogAndDiscardQuits) {
    for (const auto& surface : dialogSurfaces()) {
        if (!surface.open || surface.name != "Settings")
            continue;
        MainComponent mc(std::make_unique<MockProvider>());
        mc.setSize(1600, 900);
        mc.getAudioEngine().suspendDeviceCallback();
        makeDirty(mc);
        SweepEnvironment env(synth::theme::makeDaylight());
        auto* dialog = surface.open(env);
        ASSERT_NE(dialog, nullptr);

        int quits = 0;
        synth::requestQuit(&mc, [&quits] { ++quits; });
        juce::AlertWindow* alert = nullptr;
        synth::test::waitUntil([&] {
            for (int i = 0; i < juce::Desktop::getInstance().getNumComponents(); ++i)
                if (auto* a = dynamic_cast<juce::AlertWindow*>(juce::Desktop::getInstance().getComponent(i)))
                    alert = a;
            return alert != nullptr;
        });
        ASSERT_NE(alert, nullptr) << "the unsaved-changes question must be on screen";
        EXPECT_EQ(juce::Component::getCurrentlyModalComponent(), alert) << "it is the modal one, so it takes the click";
#if JUCE_MAC // window stacking is read from the native windows on macOS only; elsewhere the check cannot tell
        EXPECT_TRUE(PopupMotion::isAboveForTest(*alert, *dialog)) << "it is not buried under the dialog";
#endif
        auto* discard = alert->getButton("Discard");
        ASSERT_NE(discard, nullptr);
        discard->triggerClick();
        synth::test::waitUntil([&] { return quits == 1; });
        EXPECT_EQ(quits, 1);
        synth::ui::deleteOpenModalDialogs();
        EXPECT_EQ(juce::ModalComponentManager::getInstance()->getNumModalComponents(), 0);
    }
}
