// CardCallOutRelayTests.cpp -- the shortcut-relay rule for popovers. A parentless juce::CallOutBox is its
// own top-level window, so Cmd+Z / Cmd+Shift+Z pressed in it never reached the app (ShortcutRelayTests covers the
// dialogs and tool windows; this covers the popovers a card opens):
//   - a sequencer's Randomize panel hands undo and redo to the main window from every focusable child, stays open,
//     and shows the range the undo gave back;
//   - every CallOutBox opened from a card goes through launchCardCallOut: a raw launch must be registered below with
//     the reason it is not a card popover or does not relay.
#include "../../TestWait.h"
#include "../ModuleViews/StepGrid/StepGridCardFixture.h"
#include "../SourceScan.h"
#include "ShortcutManager/ShortcutManager.h"
#include "UI/Graph/CanvasCardKeyboard/CanvasCardKeyboard.h"
#include "UI/Layout/CardCallOut.h"
#include "UI/Layout/FocusStepWithin.h"
#include <map>
#include <regex>

using stepgrid_test::SeqCard;
using synth::ui::NoteRangePicker;
using synth::ui::StepGridRandomizePanel;
using synth::ui::StepGridTools;

namespace {

// Stands in for MainComponent above the canvas: Cmd+Z and Cmd+Shift+Z run the app's undo and redo.
struct UndoHost
    : juce::Component
    , juce::ApplicationCommandTarget {
    explicit UndoHost(SeqCard& c)
        : card(c) {
        c.canvas.editor.getCardKeyboard().setShortcutManager(&shortcuts);
        addAndMakeVisible(c.canvas.editor);
        setSize(2400, 1800);
        addAndMakeVisible(box_host);
        box_host.setBounds(getLocalBounds());
    }
    bool keyPressed(const juce::KeyPress& key) override {
        received.push_back(key);
        if (key == juce::KeyPress('z', juce::ModifierKeys::commandModifier, 0))
            return card.canvas.undo.getUndoManager().undo();
        if (key == juce::KeyPress('z', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0))
            return card.canvas.undo.getUndoManager().redo();
        return false;
    }
    ApplicationCommandTarget* getNextCommandTarget() override { return nullptr; }
    void getAllCommands(juce::Array<juce::CommandID>&) override {}
    void getCommandInfo(juce::CommandID, juce::ApplicationCommandInfo&) override {}
    bool perform(const InvocationInfo&) override { return false; }

    SeqCard& card;
    ShortcutManager shortcuts;
    juce::Component box_host;
    std::vector<juce::KeyPress> received;
};

// A real key's path: the focused component, then each parent up to the box, where the box's key listeners see it
// before its own keyPressed.
bool dispatchKey(juce::CallOutBox& box, juce::Component& origin, const juce::KeyPress& press) {
    for (auto* target = &origin; target != nullptr; target = target->getParentComponent()) {
        if (target == &box && synth::ui::test_hooks::offerKeyToCardCallOutForTest(box, press))
            return true;
        if (target->keyPressed(press))
            return true;
    }
    return false;
}

const juce::KeyPress kUndo('z', juce::ModifierKeys(juce::ModifierKeys::commandModifier), 0);
const juce::KeyPress
    kRedo('z', juce::ModifierKeys(juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier), 0);

// The Randomize panel opened through the card's own tool button, in a CallOutBox that sits on `host`.
struct OpenPanel {
    OpenPanel(SeqCard& c, UndoHost& host) {
        synth::ui::test_hooks::cardCallOutLauncherForTest() = [&](std::unique_ptr<juce::Component> content,
                                                                  juce::Rectangle<int> area) {
            panel = dynamic_cast<StepGridRandomizePanel*>(content.get());
            owned = std::move(content);
            box = std::make_unique<juce::CallOutBox>(*owned, area, &host.box_host);
            return box.get();
        };
        c.grid->getTools().getButton(StepGridTools::Tool::Randomize).onClick();
        synth::ui::test_hooks::cardCallOutLauncherForTest() = nullptr;
    }
    ~OpenPanel() {
        box.reset();
        owned.reset();
    }
    std::unique_ptr<juce::Component> owned;
    std::unique_ptr<juce::CallOutBox> box;
    StepGridRandomizePanel* panel = nullptr;
};

void dragBandLeft(NoteRangePicker& picker) {
    const auto mods = juce::ModifierKeys::leftButtonModifier;
    const float from = picker.bandBounds().getCentreX();
    const float to = from - picker.whiteKeyWidth() * 7.0f; // one octave: the band moves by octaves
    const auto at = [&](float x) { return cardbody_test::mouseAt(picker, {x, 17.0f}, mods); };
    picker.mouseDown(at(from));
    picker.mouseDrag(at((from + to) * 0.5f));
    picker.mouseDrag(at(to));
    picker.mouseUp(at(to));
}

} // namespace

TEST(CardCallOutRelay, CmdZAndCmdShiftZFromAnyFocusableChildOfTheRandomizePanelUndoAndRedoTheRangeAndKeepItOpen) {
    SeqCard c;
    UndoHost host(c);
    OpenPanel open(c, host);
    ASSERT_NE(open.panel, nullptr);
    ASSERT_NE(open.box, nullptr);
    auto& picker = open.panel->getRangePicker();
    const int low = picker.getLow(), high = picker.getHigh();

    dragBandLeft(picker);
    const int movedLow = picker.getLow(), movedHigh = picker.getHigh();
    ASSERT_NE(movedLow, low) << "the drag moved the band";
    ASSERT_TRUE(c.canvas.undo.canUndo()) << "and saved it as an undo step";

    const auto stops = synth::ui::focusableDescendants(*open.panel);
    ASSERT_GE(stops.size(), 5u) << "the three range stops, the slider, the toggle and Roll";
    for (auto* stop : stops) {
        SCOPED_TRACE(stop->getTitle().toStdString());
        host.received.clear();
        EXPECT_TRUE(dispatchKey(*open.box, *stop, kUndo));
        synth::test::waitUntil([&] { return host.received.size() == 1u; });
        c.grid->poll(); // the card's own timer
        EXPECT_EQ(host.received.size(), 1u) << "the app got Cmd+Z";
        EXPECT_EQ(picker.getLow(), low) << "undo gave the range back, in the panel that stayed open";
        EXPECT_EQ(picker.getHigh(), high);
        EXPECT_TRUE(open.box->isParentOf(open.panel)) << "panel still open";

        EXPECT_TRUE(dispatchKey(*open.box, *stop, kRedo));
        synth::test::waitUntil([&] { return host.received.size() == 2u; });
        c.grid->poll();
        EXPECT_EQ(picker.getLow(), movedLow) << "redo moved it again";
        EXPECT_EQ(picker.getHigh(), movedHigh);
        EXPECT_TRUE(open.box->isParentOf(open.panel));
    }
}

TEST(CardCallOutRelay, KeysThatActOnTheCanvasOrAreTypedStayWithThePanel) {
    SeqCard c;
    UndoHost host(c);
    OpenPanel open(c, host);
    ASSERT_NE(open.box, nullptr);
    const auto cmd = juce::ModifierKeys::commandModifier;
    auto* stop = synth::ui::focusableDescendants(*open.panel).front();
    host.received.clear();
    for (const auto& key : {juce::KeyPress('a', juce::ModifierKeys(cmd), 0),
                            juce::KeyPress('c', juce::ModifierKeys(cmd), 0), juce::KeyPress(juce::KeyPress::deleteKey)})
        EXPECT_FALSE(synth::ui::test_hooks::offerKeyToCardCallOutForTest(*open.box, key)) << key.getTextDescription();
    EXPECT_TRUE(host.received.empty()) << "declined at once, nothing queued";
    EXPECT_NE(stop, nullptr);
}

// Every CallOutBox::launchAsynchronously call site in Source, by file. A popover a card opens uses launchCardCallOut
// and so is not here. A new raw launch fails until it is registered with the reason it does not relay.
TEST(CardCallOutRelay, EveryRawCallOutLaunchIsRegisteredWithItsReason) {
    struct Raw {
        const char* file;
        int calls;
        const char* reason;
    };
    const char* kSettings = "Settings window: its own window, not over the canvas";
    const char* kEditing = "a layout editing session: an undo would rewrite the layout being edited";
    const char* kMain = "opened from a main-window panel, not a card: relay not wired yet";
    const std::vector<Raw> raw = {
        {"Source/UI/Settings/AppearanceSettingsTab.cpp", 1, kSettings},
        {"Source/UI/Settings/AccountSettingsTab.cpp", 1, kSettings},
        {"Source/UI/Settings/PreferencesSettingsTab/PreferencesSettingsTabLifecycle.cpp", 1, kSettings},
        {"Source/UI/Layout/CardCallOut.cpp", 1, "launchCardCallOut itself"},
        {"Source/UI/Graph/ModuleComponent/ModuleComponentLayoutEditor.cpp", 1, kEditing},
        {"Source/UI/Graph/CardLayoutEditor/OnCard/CardLayoutOnCardEditorPanel.cpp", 1, kEditing},
        {"Source/UI/Graph/ModMatrixComponent.cpp", 1, kMain},
        {"Source/UI/Graph/GraphEditor/GraphEditorMacroPrompts.cpp", 1,
         "macro prompt: answers a question about the canvas"},
        {"Source/UI/Macros/MacroPortConfigDialog/MacroPortConfigDialogInternal.h", 1, "inside a modal dialog"},
        {"Source/UI/PianoRoll/ScaleAssistPanel.cpp", 1, kMain},
        {"Source/UI/MidiRemote/MidiRemotePanel/MidiRemotePanelControllers.cpp", 1, kMain},
        {"Source/UI/MidiRemote/MidiRemotePanel/MidiRemotePanelAssign.cpp", 1, kMain},
        {"Source/UI/Chrome/ColourPickerPopup.h", 1, kMain},
        {"Source/UI/Timeline/TimelineRulerComponent.cpp", 1, kMain},
        {"Source/UI/Timeline/SongKeyField.cpp", 1, kMain},
        {"Source/UI/Timeline/TimelineTrackHeaderComponent/TimelineTrackHeaderComponent.cpp", 2, kMain},
        {"Source/UI/Timeline/TimelinePanelComponent/AddTrackPicker.cpp", 1, kMain},
        {"Source/UI/Timeline/TimelinePanelComponent/TimelinePanelAddAutomation.cpp", 1, kMain},
        {"Source/UI/Timeline/TimelineRoutingPane/TimelineRoutingPaneActions.cpp", 1, kMain},
        {"Source/UI/Timeline/AutomationLanes/AutomationLaneHeader/AutomationLaneHeaderParameterPicker.cpp", 1, kMain},
        {"Source/UI/Timeline/AutomationLanes/AutomationLaneHeader/AutomationLaneHeaderMenu.cpp", 1, kMain},
        {"Source/UI/Timeline/AutomationLanes/Modulators/ModulatorRowEdits.cpp", 1, kMain},
        {"Source/UI/Mixer/MixerInsertList/MixerInsertListEdits.cpp", 1, kMain},
        {"Source/UI/Mixer/MixerPanelComponent/MixerPanelColour.cpp", 1, kMain},
    };
    std::map<std::string, int> registered;
    for (const auto& r : raw) {
        registered[r.file] = r.calls;
        EXPECT_GT(std::string(r.reason).size(), 10u) << r.file;
    }
    synth::test::source_scan::expectCountsMatch(
        "CallOutBox::launchAsynchronously",
        synth::test::source_scan::countCallSites(std::regex(R"(CallOutBox::launchAsynchronously\s*\()"), ""),
        registered, "CardCallOutRelayTests.cpp (raw) - a popover a card opens goes through launchCardCallOut");
}
