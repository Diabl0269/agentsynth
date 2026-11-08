// PopoverRelayTests.cpp -- the shortcut-relay rule for popovers. A parentless juce::CallOutBox is its
// own top-level window, so Cmd+Z / Cmd+Shift+Z pressed in it never reached the app (ShortcutRelayTests covers the
// dialogs and tool windows; this covers the popovers a card opens):
//   - a sequencer's Randomize panel hands undo and redo to the main window from every focusable child, stays open,
//     and shows the range the undo gave back;
//   - the popovers the main window opens (timeline, mixer, MIDI Remote, key pickers) do the same from a real
//     MainComponent: Cmd+Z undoes, Cmd+Shift+Z redoes, Space plays and stops, a popover that would show a stale
//     project closes instead;
//   - every popover goes through launchPopover (PopupOwnPathTests scans for a raw launch).
#include "../../TestWait.h"
#include "../Mixer/MixerHeaderTestRig.h"
#include "../ModuleViews/StepGrid/StepGridCardFixture.h"
#include "MainPopoverKeys.h"
#include "ShortcutManager/ShortcutManager.h"
#include "UI/Graph/CanvasCardKeyboard/CanvasCardKeyboard.h"
#include "UI/Layout/FocusStepWithin.h"
#include "UI/Layout/PopupHost.h"
#include "UI/PianoRoll/PianoRollComponent/PianoRollComponent.h"
#include "UI/Timeline/SongKeyField.h"
#include "UI/Timeline/TimelinePanelComponent/TimelinePanelComponent.h"

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
        if (target == &box && synth::ui::test_hooks::offerKeyToPopoverForTest(box, press))
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
        synth::ui::test_hooks::popoverLauncherForTest() = [&](std::unique_ptr<juce::Component> content,
                                                              juce::Rectangle<int> area) {
            panel = dynamic_cast<StepGridRandomizePanel*>(content.get());
            owned = std::move(content);
            box = std::make_unique<juce::CallOutBox>(*owned, area, &host.box_host);
            return box.get();
        };
        c.grid->getTools().getButton(StepGridTools::Tool::Randomize).onClick();
        synth::ui::test_hooks::popoverLauncherForTest() = nullptr;
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

TEST(PopoverRelay, CmdZAndCmdShiftZFromAnyFocusableChildOfTheRandomizePanelUndoAndRedoTheRangeAndKeepItOpen) {
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

TEST(PopoverRelay, KeysThatActOnTheCanvasOrAreTypedStayWithThePanel) {
    SeqCard c;
    UndoHost host(c);
    OpenPanel open(c, host);
    ASSERT_NE(open.box, nullptr);
    const auto cmd = juce::ModifierKeys::commandModifier;
    auto* stop = synth::ui::focusableDescendants(*open.panel).front();
    host.received.clear();
    for (const auto& key : {juce::KeyPress('a', juce::ModifierKeys(cmd), 0),
                            juce::KeyPress('c', juce::ModifierKeys(cmd), 0), juce::KeyPress(juce::KeyPress::deleteKey)})
        EXPECT_FALSE(synth::ui::test_hooks::offerKeyToPopoverForTest(*open.box, key)) << key.getTextDescription();
    EXPECT_TRUE(host.received.empty()) << "declined at once, nothing queued";
    EXPECT_NE(stop, nullptr);
}

// ---- The popovers the main window opens ------------------------------------------------------------------------
namespace {

using main_popover::Capture;
using Tab = synth::ui::BottomDockComponent::Tab;

juce::Component* findById(juce::Component& root, const juce::String& id) {
    for (auto* child : root.getChildren()) {
        if (child->getComponentID() == id)
            return child;
        if (auto* deeper = findById(*child, id))
            return deeper;
    }
    return nullptr;
}

template <class T>
T* findFirst(juce::Component& root) {
    for (auto* child : root.getChildren()) {
        if (auto* hit = dynamic_cast<T*>(child))
            return hit;
        if (auto* deeper = findFirst<T>(*child))
            return deeper;
    }
    return nullptr;
}

class MainPopoverRelay : public main_popover::MainPopoverTest {
protected:
    // Two audio tracks for the strips and headers: the second one's add is the undo step the keys take back and bring
    // back, and the first track's popovers stay valid across it.
    void SetUp() override {
        MainPopoverTest::SetUp();
        mc_->simulateAddAudioTrackClick();
        main_popover::pump();
        mc_->simulateAddAudioTrackClick();
        main_popover::pump();
    }

    // Opens the popover `open` makes, presses Space and the canvas keys in it, then Cmd+Z (the popover closes when its
    // content would be stale), reopens it and presses Cmd+Shift+Z.
    void exercise(const std::string& name, const std::function<void()>& open, Capture& popover, bool goesStale) {
        SCOPED_TRACE(name);
        open();
        ASSERT_NE(popover.box(), nullptr) << name << ": the real button did not open a popover";
        ASSERT_TRUE(popover.box()->isParentOf(popover.content()));
        expectSpacePlaysAndStops(popover, name);
        expectCanvasKeysStay(popover, name);
        expectUndoReachesTheApp(popover, name, goesStale);
        popover.closeNow();
        open();
        ASSERT_NE(popover.box(), nullptr) << name;
        expectRedoReachesTheApp(popover, name);
        popover.closeNow();
        EXPECT_FALSE(undone());
    }
};

} // namespace

TEST_F(MainPopoverRelay, TheTimelineTrackColourPickerTakesCmdZCmdShiftZAndSpaceFromTheApp) {
    mc_->setMainViewForTest(Tab::Timeline);
    mc_->resized();
    synth::test::waitUntil([&] { return findById(*mc_, "trackColourSwatch") != nullptr; });
    auto* swatch = dynamic_cast<juce::Button*>(findById(*mc_, "trackColourSwatch"));
    ASSERT_NE(swatch, nullptr);
    Capture popover(*mc_);
    exercise("track colour swatch", [&] { swatch->onClick(); }, popover, true);
}

TEST_F(MainPopoverRelay, TheTransportKeyPickerTakesCmdZCmdShiftZAndSpaceFromTheApp) {
    mc_->setMainViewForTest(Tab::Timeline);
    mc_->resized();
    synth::test::waitUntil([&] { return findFirst<synth::ui::SongKeyField>(*mc_) != nullptr; });
    auto* field = findFirst<synth::ui::SongKeyField>(*mc_);
    ASSERT_NE(field, nullptr);
    Capture popover(*mc_);
    exercise("song key field", [&] { field->openPicker(); }, popover, true);
}

TEST_F(MainPopoverRelay, TheMixerColourPickerAndInsertPickerTakeCmdZCmdShiftZAndSpaceFromTheApp) {
    auto& panel = mc_->getBottomDock().getMixerPanel();
    panel.setSize(1400, 700);
    panel.resized();
    panel.rebuild();
    synth::test::waitUntil([&] { return panel.getStripColumnForTest(0) != nullptr; });
    auto* column = panel.getStripColumnForTest(0);
    ASSERT_NE(column, nullptr);
    Capture popover(*mc_);
    exercise(
        "mixer colour dot",
        [&] {
            mixer_header_test::clickThroughMouse(
                static_cast<juce::Component&>(column->getHeaderForTest().getColourDotForTest()));
        },
        popover, true);
    ASSERT_TRUE(column->getInsertList().hasAddRow());
    exercise("mixer + Insert row", [&] { column->getInsertList().activateAddRow(); }, popover, true);
}

TEST_F(MainPopoverRelay, TheScaleAssistKeyPickerTakesCmdZCmdShiftZAndSpaceFromTheApp) {
    mc_->setMainViewForTest(Tab::Timeline);
    auto& doc = mc_->getTimelineDoc();
    const auto track = doc.addTrack(synth::TrackKind::Midi, "Track 1");
    const auto clip = doc.addClip(track, 0.0, 8.0, "Clip");
    auto& timeline = mc_->getTimelinePanel();
    timeline.selectTracks({track}, track);
    timeline.openPianoRoll(clip);
    timeline.getPianoRoll().toggleScalePanel();
    mc_->resized();
    synth::test::waitUntil([&] { return timeline.getPianoRoll().isScalePanelShowing(); });
    Capture popover(*mc_);
    exercise("scale assist key", [&] { timeline.getPianoRoll().getScaleAssistPanel().openKeyPicker(); }, popover, true);
}
