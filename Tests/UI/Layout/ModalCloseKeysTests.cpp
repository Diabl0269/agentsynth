// ModalCloseKeysTests.cpp -- Esc and Cmd+W close every dialog that PopupMotion::launchDialog opened, from any focused
// child, delivered in the order a real key takes (the focused component, then each parent up to the window, whose key
// listeners see it before its own keyPressed). Windows that open on a real display are skipped on a headless runner.
#include "../ModuleViews/ShapeEditor/ShapeEditorTestHelpers.h"
#include "Modules/FX/ParametricEQModule.h"
#include "Modules/SamplerModule.h"
#include "NativeWindowTestGuard.h"
#include "UI/Graph/ModuleComponent/SamplerChrome/SamplerMapView.h"
#include "UI/Graph/ModuleComponent/SamplerChrome/SamplerMapWindow.h"
#include "UI/Layout/DialogKeyboard.h"
#include "UI/Layout/FocusStepWithin.h"
#include "UI/Layout/PopupMotion.h"
#include "UI/ModuleViews/EQWindow.h"
#include "UI/ModuleViews/ShapeEditor/ShapeEditorWindow.h"
#include <gtest/gtest.h>

using namespace synth::ui;
using namespace synth::ui::test;

namespace {

juce::KeyPress escapeKey() { return juce::KeyPress(juce::KeyPress::escapeKey, juce::ModifierKeys(), 0); }
juce::KeyPress closeWindowKey() { return juce::KeyPress('W', juce::ModifierKeys::commandModifier, 0); }

// juce::ComponentPeer::handleKeyPress: each component from the focused one up offers a key to its key listeners and
// then to its own keyPressed; the first to use it ends the walk. Only the dialog's own listener is reachable here.
bool dispatchKey(juce::DialogWindow& dialog, juce::Component& origin, const juce::KeyPress& press) {
    for (auto* target = &origin; target != nullptr; target = target->getParentComponent()) {
        if (target == &dialog && PopupMotion::offerKeyToDialogForTest(dialog, press))
            return true;
        if (target->keyPressed(press))
            return true;
    }
    return false;
}

// A closed dialog is only dismissed: JUCE's modal manager deletes it on a later message-loop turn, and
// deleteOpenModalDialogs() no longer sees it. Deleting it here keeps it from outliving the model it shows.
void deleteNow(juce::Component::SafePointer<juce::DialogWindow>& dialog) {
    deleteOpenModalDialogs();
    delete dialog.getComponent();
}

// Declared after the test's own objects (a rig, a module, the content): its windows go before them, also on an early
// return. The fixture's TearDown runs only after those objects are gone.
struct DialogsDeletedFirst {
    ~DialogsDeletedFirst() { deleteOpenModalDialogs(); }
};

bool isTextEditorPart(juce::Component& c) {
    return dynamic_cast<juce::TextEditor*>(&c) != nullptr ||
           c.findParentComponentOfClass<juce::TextEditor>() != nullptr;
}

class ModalCloseKeys : public ::testing::Test {
protected:
    void SetUp() override {
        if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr)
            GTEST_SKIP() << "needs a display";
        if (nativeWindowsAbortHere())
            GTEST_SKIP() << "native windows abort the process under the ASAN job's Xvfb";
        juce::MessageManager::getInstance();
        wasEnabled_ = PopupMotion::isEnabled();
        PopupMotion::setEnabled(false); // a close takes effect at once
    }
    void TearDown() override {
        deleteOpenModalDialogs();
        PopupMotion::setEnabled(wasEnabled_);
    }

    // Esc and Cmd+W from every focusable descendant of `dialog`: each closes it (a text field keeps Escape for
    // itself), and the window is reopened by `reopen` between presses.
    void expectEveryStopCloses(const std::function<juce::DialogWindow*()>& reopen) {
        juce::Component::SafePointer<juce::DialogWindow> probe(reopen());
        ASSERT_NE(probe.getComponent(), nullptr);
        const auto stops = focusableDescendants(*probe.getComponent());
        const int count = (int)stops.size();
        deleteNow(probe);
        ASSERT_GT(count, 0);
        for (int i = 0; i < count; ++i) {
            for (const auto& press : {escapeKey(), closeWindowKey()}) {
                juce::Component::SafePointer<juce::DialogWindow> dialog(reopen());
                ASSERT_NE(dialog.getComponent(), nullptr);
                const DialogDeleter deleteAfterPress{dialog};
                auto* stop = focusableDescendants(*dialog.getComponent())[(size_t)i];
                if (isTextEditorPart(*stop) && press == escapeKey()) {
                    EXPECT_TRUE(dispatchKey(*dialog.getComponent(), *stop, press)) << "a text field uses Escape itself";
                    EXPECT_TRUE(dialog->isVisible());
                } else {
                    EXPECT_TRUE(dispatchKey(*dialog.getComponent(), *stop, press))
                        << "stop " << i << " (" << stop->getTitle() << ") press " << press.getTextDescription();
                    EXPECT_FALSE(dialog->isVisible()) << "stop " << i << " (" << stop->getTitle() << ")";
                }
            }
        }
    }

    struct DialogDeleter {
        juce::Component::SafePointer<juce::DialogWindow>& dialog;
        ~DialogDeleter() { deleteNow(dialog); }
    };

    bool wasEnabled_ = true;
};

juce::DialogWindow* launchGeneric(juce::Component& content) {
    juce::DialogWindow::LaunchOptions options;
    options.content.setNonOwned(&content);
    options.dialogTitle = "Generic";
    options.useNativeTitleBar = true;
    options.resizable = false;
    return PopupMotion::launchDialog(options);
}

struct GenericContent : juce::Component {
    GenericContent() {
        for (auto* c : std::initializer_list<juce::Component*>{&button, &toggle, &editor, &combo, &slider}) {
            c->setWantsKeyboardFocus(true);
            addAndMakeVisible(*c);
        }
        combo.addItem("one", 1);
        setSize(300, 200);
    }
    juce::TextButton button{"Go"};
    juce::ToggleButton toggle{"Flag"};
    juce::TextEditor editor;
    juce::ComboBox combo;
    juce::Slider slider;
};

} // namespace

// Regression test for FRO1175: Esc on a focused map dot only refocused the map and Cmd+W did nothing
TEST_F(ModalCloseKeys, EveryStopOfTheShapeEditorWindowClosesIt) {
    ShapeRig rig;
    const DialogsDeletedFirst dialogsFirst;
    expectEveryStopCloses([&] { return openShapeEditorWindow(rig.adapter, 0, nullptr); });
}

TEST_F(ModalCloseKeys, EveryStopOfTheSamplerMapWindowClosesIt) {
    SamplerModule sampler;
    const DialogsDeletedFirst dialogsFirst;
    expectEveryStopCloses([&] {
        auto view = std::make_unique<SamplerMapView>(
            sampler, [](const std::vector<synth::sampler::RegionSpec>&) { return true; });
        return openSamplerMapWindow(std::move(view), nullptr);
    });
}

TEST_F(ModalCloseKeys, EveryStopOfTheEQWindowClosesIt) {
    std::vector<std::unique_ptr<ParametricEQModule>> modules; // one per window: kept until the test ends
    const DialogsDeletedFirst dialogsFirst;
    expectEveryStopCloses([&] {
        modules.push_back(std::make_unique<ParametricEQModule>());
        auto& eq = *modules.back();
        juce::DialogWindow::LaunchOptions options;
        options.content.setOwned(new EQWindow(eq));
        options.dialogTitle = "EQ";
        options.useNativeTitleBar = true;
        return PopupMotion::launchDialog(options);
    });
}

TEST_F(ModalCloseKeys, EveryStopOfAGenericDialogClosesIt) {
    GenericContent content;
    const DialogsDeletedFirst dialogsFirst;
    expectEveryStopCloses([&] { return launchGeneric(content); });
}

TEST_F(ModalCloseKeys, TheListHoldsExactlyTheDialogsLaunchedThroughIt) {
    GenericContent content;
    const DialogsDeletedFirst dialogsFirst;
    const auto before = PopupMotion::openDialogsForTest().size();
    auto* dialog = launchGeneric(content);
    ASSERT_NE(dialog, nullptr);
    ASSERT_EQ(PopupMotion::openDialogsForTest().size(), before + 1);
    EXPECT_EQ(PopupMotion::openDialogsForTest().back(), dialog);
    EXPECT_FALSE(PopupMotion::offerKeyToDialogForTest(*dialog, juce::KeyPress('x', juce::ModifierKeys(), 'x')));
    EXPECT_TRUE(dialog->isVisible());
    EXPECT_FALSE(PopupMotion::offerKeyToDialogForTest(
        *dialog, juce::KeyPress('W', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0)))
        << "Cmd+Shift+W is not Cmd+W";
}

TEST_F(ModalCloseKeys, EscapeInARenameEditorCancelsTheRenameAndKeepsTheWindow) {
    ShapeRig rig;
    const DialogsDeletedFirst dialogsFirst;
    auto* dialog = openShapeEditorWindow(rig.adapter, 0, nullptr);
    ASSERT_NE(dialog, nullptr);
    auto* editor = dynamic_cast<ShapeListEditor*>(dialog->getContentComponent());
    ASSERT_NE(editor, nullptr);
    auto& map = editor->getMap();
    const auto before = rig.adapter->getShapes()[0].name;
    map.beginRename(0);
    ASSERT_TRUE(map.isRenaming());
    juce::TextEditor* rename = nullptr;
    for (auto* child : map.getChildren())
        if (auto* te = dynamic_cast<juce::TextEditor*>(child))
            rename = te;
    ASSERT_NE(rename, nullptr);
    rename->setText("renamed", false);

    EXPECT_TRUE(dispatchKey(*dialog, *rename, escapeKey()));
    for (int i = 0; i < 100 && map.isRenaming(); ++i)
        juce::MessageManager::getInstance()->runDispatchLoopUntil(20); // a text field reports Escape a turn later

    EXPECT_TRUE(dialog->isVisible());
    EXPECT_FALSE(map.isRenaming());
    EXPECT_EQ(rig.adapter->getShapes()[0].name, before);
}

TEST_F(ModalCloseKeys, EscapeInATextEditorDoesNotCloseTheDialog) {
    GenericContent content;
    const DialogsDeletedFirst dialogsFirst;
    auto* dialog = launchGeneric(content);
    ASSERT_NE(dialog, nullptr);
    EXPECT_TRUE(dispatchKey(*dialog, content.editor, escapeKey())) << "the editor itself uses it";
    EXPECT_TRUE(dialog->isVisible());
}

namespace {

// Paints a window's content into a software bitmap, as the screen would, and moves a band so its curve repaints.
void paintContent(juce::Component& content) {
    juce::Image img(juce::Image::ARGB, std::max(1, content.getWidth()), std::max(1, content.getHeight()), true,
                    juce::SoftwareImageType());
    juce::Graphics g(img);
    content.paintEntireComponent(g, false);
}

void feedAudio(ParametricEQModule& eq) {
    eq.prepareToPlay(48000.0, 512);
    juce::AudioBuffer<float> audio(2, 512);
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 512; ++i)
            audio.setSample(ch, i, 0.5f * std::sin(0.05f * (float)i));
    juce::MidiBuffer midi;
    eq.processBlock(audio, midi);
}

} // namespace

// Regression test for FRO1296: an EQ window opened on a module that already had one open and closed crashed in paint
TEST_F(ModalCloseKeys, AnEQWindowOpenedAgainOnTheSameModuleClosesPaintsAndEdits) {
    ParametricEQModule eq;
    feedAudio(eq);
    const DialogsDeletedFirst dialogsFirst;
    for (int round = 0; round < 4; ++round) {
        juce::DialogWindow::LaunchOptions options;
        auto* window = new EQWindow(eq);
        options.content.setOwned(window);
        options.dialogTitle = "EQ";
        options.useNativeTitleBar = true;
        juce::Component::SafePointer<juce::DialogWindow> dialog(PopupMotion::launchDialog(options));
        ASSERT_NE(dialog.getComponent(), nullptr);
        window->getCurve().addPointAt({100.0f + 20.0f * (float)round, 80.0f});
        feedAudio(eq);
        window->getCurve().timerCallback();
        paintContent(*window);
        if (round % 2 == 0) { // alternate: closed by key, and deleted while still shown
            EXPECT_TRUE(dispatchKey(*dialog.getComponent(), window->getCurve(), escapeKey()));
            EXPECT_FALSE(dialog->isVisible());
        }
        deleteNow(dialog);
    }
}

TEST_F(ModalCloseKeys, AShapeEditorWindowOpenedAgainOnTheSameModuleClosesAndPaints) {
    ShapeRig rig;
    const DialogsDeletedFirst dialogsFirst;
    for (int round = 0; round < 4; ++round) {
        juce::Component::SafePointer<juce::DialogWindow> dialog(openShapeEditorWindow(rig.adapter, 0, nullptr));
        ASSERT_NE(dialog.getComponent(), nullptr);
        paintContent(*dialog->getContentComponent());
        if (round % 2 == 0) { // alternate: closed by key, and deleted while still shown
            auto* stop = focusableDescendants(*dialog.getComponent())[0];
            EXPECT_TRUE(dispatchKey(*dialog.getComponent(), *stop, escapeKey()));
            EXPECT_FALSE(dialog->isVisible());
        }
        deleteNow(dialog);
    }
}
