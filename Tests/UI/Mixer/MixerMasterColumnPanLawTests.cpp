// The mixer's pan-law control, on Master's column next to its mute button -- there is no other
// project-settings surface today, so this is where it lives. Same fixture/click idiom as
// MixerMasterColumnMidiLearnTests.cpp's right-click menu (see docs/mixer/mixer.md#pan-law).

#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "Modules/MasterModule.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Mixer/MixerMasterColumn.h"

#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>

namespace {

struct MasterColumnPanLawFixture {
    AudioEngine engine;
    AppUndoManager undoManager;
    GraphEditor editor{engine, &undoManager};
    synth::ui::MixerMasterColumn column;
    synth::MacroSet macros;
    juce::AudioProcessorGraph::Node::Ptr masterNode;

    MasterColumnPanLawFixture() {
        auto& graph = engine.getGraph();
        graph.setPlayConfigDetails(0, 2, 44100.0, 512);
        editor.setSize(900, 600);
        masterNode = graph.addNode(std::make_unique<MasterModule>());
        column.configure(graph, undoManager, macros, editor, engine);
        column.setSize(140, 300);
        column.setNodeId(masterNode->nodeID);
    }

    // Clicks the pan-law button, capturing whichever menu it opens.
    juce::PopupMenu openPanLawMenu() {
        juce::PopupMenu captured;
        column.setShowPanLawMenuHookForTest([&](juce::PopupMenu& menu) { captured = menu; });
        column.getPanLawButtonForTest().onClick();
        column.setShowPanLawMenuHookForTest(nullptr);
        return captured;
    }

    // Finds and invokes the menu item whose text starts with `prefix` (the two entries are
    // "Balance (legacy)" and "Compensated (...)").
    static bool pick(const juce::PopupMenu& menu, const juce::String& prefix) {
        juce::PopupMenu::MenuItemIterator it(menu);
        while (it.next()) {
            if (it.getItem().text.startsWith(prefix)) {
                it.getItem().action();
                return true;
            }
        }
        return false;
    }
};

} // namespace

TEST(MixerMasterColumnPanLawTests, ButtonLabelsBalanceByDefault) {
    MasterColumnPanLawFixture fixture;
    EXPECT_EQ(fixture.column.getPanLawButtonForTest().getButtonText(), "Pan: Bal.");
}

TEST(MixerMasterColumnPanLawTests, MenuTicksTheCurrentLaw) {
    MasterColumnPanLawFixture fixture;
    fixture.engine.setMixerPanLaw(synth::MixerPanLaw::Compensated);

    const auto menu = fixture.openPanLawMenu();
    juce::PopupMenu::MenuItemIterator it(menu);
    bool sawTickedCompensated = false;
    while (it.next()) {
        if (it.getItem().text.startsWith("Compensated"))
            sawTickedCompensated = it.getItem().isTicked;
    }
    EXPECT_TRUE(sawTickedCompensated);
}

TEST(MixerMasterColumnPanLawTests, PickingCompensatedSetsTheEngineAndRelabelsTheButton) {
    MasterColumnPanLawFixture fixture;
    ASSERT_EQ(fixture.engine.getMixerPanLaw(), synth::MixerPanLaw::Balance);

    const auto menu = fixture.openPanLawMenu();
    ASSERT_TRUE(MasterColumnPanLawFixture::pick(menu, "Compensated"));

    EXPECT_EQ(fixture.engine.getMixerPanLaw(), synth::MixerPanLaw::Compensated);
    EXPECT_EQ(fixture.column.getPanLawButtonForTest().getButtonText(), "Pan: Comp.");
}

// The project's ONE dirty-state funnel is the undo edit serial (docs/architecture/project-bundle.md
// #dirty-state-and-the-unsaved-changes-guard) -- picking a different law must bump it, or the
// document would not read as dirty after the change.
TEST(MixerMasterColumnPanLawTests, PickingADifferentLawBumpsTheEditSerial) {
    MasterColumnPanLawFixture fixture;
    const int serialBefore = fixture.undoManager.getEditSerial();

    const auto menu = fixture.openPanLawMenu();
    ASSERT_TRUE(MasterColumnPanLawFixture::pick(menu, "Compensated"));

    EXPECT_GT(fixture.undoManager.getEditSerial(), serialBefore);
}

TEST(MixerMasterColumnPanLawTests, PickingTheSameLawIsANoOp) {
    MasterColumnPanLawFixture fixture;
    ASSERT_EQ(fixture.engine.getMixerPanLaw(), synth::MixerPanLaw::Balance);
    const int serialBefore = fixture.undoManager.getEditSerial();

    const auto menu = fixture.openPanLawMenu();
    ASSERT_TRUE(MasterColumnPanLawFixture::pick(menu, "Balance"));

    EXPECT_EQ(fixture.undoManager.getEditSerial(), serialBefore)
        << "re-picking the already-active law must not record an empty undo step";
}

TEST(MixerMasterColumnPanLawTests, UndoRestoresThePreviousLaw) {
    MasterColumnPanLawFixture fixture;
    const auto menu = fixture.openPanLawMenu();
    ASSERT_TRUE(MasterColumnPanLawFixture::pick(menu, "Compensated"));
    ASSERT_EQ(fixture.engine.getMixerPanLaw(), synth::MixerPanLaw::Compensated);

    fixture.undoManager.getUndoManager().undo();
    EXPECT_EQ(fixture.engine.getMixerPanLaw(), synth::MixerPanLaw::Balance);
}
