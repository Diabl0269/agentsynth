// EditorHeaderTabReachTests.cpp -- every control in a clip editor's header is a named Tab stop.
//
// The app's Tab is the region cycle, so a control that merely wants keyboard focus is not reachable: the editor's
// EditorTabWalk has to claim Tab from the grid. For each clip editor the timeline hosts (one row each below), the test
// enumerates the header's controls from the component tree, then walks Tab from the grid through the real dispatch
// order (from the focused control up through its parents, each one's key listeners before its own keyPressed) and
// expects every control to be landed on in screen order, named, with a tooltip, and the walk to come back to the grid
// and then leave. Shift+Tab walks the same stops backwards. The header is a bar of controls, so Right and Left walk the
// same stops too (a control that keeps its arrows is stepped by Alt+Right and Alt+Left), and past either end they hand
// focus back to the grid. A new editor is one more row; a new header control is found without being listed.
#include "AppUndoManager.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "UI/DrumMachine/DrumMachineComponent.h"
#include "UI/Graph/CardWidgets/CardSegmentedSwitch.h"
#include "UI/Layout/EditorTabWalk.h"
#include "UI/Layout/FadeVisibility.h"
#include "UI/PianoRoll/PianoRollComponent/PianoRollComponent.h"
#include "UI/Timeline/TimelinePanelComponent/TimelinePanelComponent.h"
#include "UI/Timeline/TimelineTrackHeaderComponent/TimelineTrackHeaderComponent.h"
#include <algorithm>
#include <functional>
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <string>
#include <vector>

namespace {

struct NullHost : synth::ui::TrackHeaderHost {
    std::vector<BindingOption> getAvailableTrackInNodes(synth::TrackId) override { return {}; }
    juce::String getNodeDisplayName(const juce::String&) override { return {}; }
    void bindTrackTo(synth::TrackId, const juce::String&) override {}
    void createAndBindTrackInNode(synth::TrackId) override {}
    void selectNodeInGraph(const juce::String&) override {}
    void deleteTrack(synth::TrackId) override {}
    void performTrackEdit(const std::function<void()>& mutation) override {
        if (mutation)
            mutation();
    }
    void addMidiTrack() override {}
    void addAudioTrack() override {}
    void addInstrumentTrack(const juce::String&, bool) override {}
    std::vector<PluginLaneOption> getAvailablePluginLaneOptions() const override { return {}; }
    synth::LaneId addPluginAutomationLane(const PluginLaneOption&) override { return {}; }
};

struct PanelWithClip {
    synth::TimelineDoc doc;
    AppUndoManager undo;
    NullHost host;
    synth::ui::TimelinePanelComponent panel;
    synth::ClipId clip;

    PanelWithClip() {
        synth::ui::FadeVisibility::setAnimateOffScreenForTest(false);
        panel.setSize(1400, 420);
        panel.setTrackHeaderHost(&host);
        panel.setTimelineDoc(&doc);
        panel.setUndoManager(&undo);
        const auto track = doc.addTrack(synth::TrackKind::Midi, "Track 1");
        clip = doc.addClip(track, 0.0, 8.0, "Clip");
    }
    ~PanelWithClip() { panel.setTrackHeaderHost(nullptr); }
};

// One clip editor: how to open it, its grid, its walk and the height of its header row.
struct Editor {
    const char* name;
    std::function<juce::Component&(PanelWithClip&)> open;
    std::function<synth::ui::EditorTabWalk&(PanelWithClip&)> walk;
    int headerHeight;
};

const std::vector<Editor>& editors() {
    static const std::vector<Editor> rows = {
        {"drum machine",
         [](PanelWithClip& f) -> juce::Component& {
             f.panel.openDrumMachine(f.clip);
             return f.panel.getDrumMachine();
         },
         [](PanelWithClip& f) -> synth::ui::EditorTabWalk& { return f.panel.getDrumMachine().getTabWalk(); },
         synth::ui::DrumMachineComponent::kToolbarHeight},
        {"piano roll",
         [](PanelWithClip& f) -> juce::Component& {
             f.panel.openPianoRoll(f.clip);
             return f.panel.getPianoRoll();
         },
         [](PanelWithClip& f) -> synth::ui::EditorTabWalk& { return f.panel.getPianoRoll().getTabWalkForTest(); },
         synth::ui::PianoRollComponent::kToolbarHeight},
    };
    return rows;
}

bool isControl(juce::Component& c) {
    return dynamic_cast<juce::Button*>(&c) != nullptr || dynamic_cast<juce::Slider*>(&c) != nullptr ||
           dynamic_cast<juce::ComboBox*>(&c) != nullptr || dynamic_cast<juce::TextEditor*>(&c) != nullptr ||
           dynamic_cast<synth::ui::CardSegmentedSwitch*>(&c) != nullptr;
}

// Every shown, enabled control under `editor` whose centre is in its header row; a control's own parts are not looked
// into.
void collectHeaderControls(juce::Component& editor, juce::Component& node, int headerHeight,
                           std::vector<juce::Component*>& out) {
    for (auto* child : node.getChildren()) {
        if (!child->isVisible() || child->getBounds().isEmpty())
            continue;
        if (isControl(*child)) {
            const auto centre = editor.getLocalPoint(child->getParentComponent(), child->getBounds().getCentre());
            if (child->isEnabled() && centre.y < headerHeight)
                out.push_back(child);
            continue;
        }
        collectHeaderControls(editor, *child, headerHeight, out);
    }
}

// What a screen reader reads for the control: its title, else a button's text.
std::string nameOf(juce::Component& c) {
    if (c.getTitle().isNotEmpty())
        return c.getTitle().toStdString();
    if (auto* button = dynamic_cast<juce::Button*>(&c))
        return button->getButtonText().toStdString();
    return {};
}

// The key travels as juce::ComponentPeer::handleKeyPress sends it, stopping at the editor: its listeners (the walk)
// see it only once it has bubbled up to the editor.
bool deliver(synth::ui::EditorTabWalk& walk, juce::Component& editor, juce::Component& focused,
             const juce::KeyPress& key) {
    for (auto* target = &focused; target != nullptr; target = target->getParentComponent()) {
        if ((target == &editor || walk.isWatching(*target)) && walk.keyPressed(key, target))
            return true;
        if (target->keyPressed(key))
            return true;
        if (target == &editor)
            return false;
    }
    return false;
}

const juce::KeyPress kTab(juce::KeyPress::tabKey, juce::ModifierKeys::noModifiers, 0);
const juce::KeyPress kShiftTab(juce::KeyPress::tabKey, juce::ModifierKeys::shiftModifier, 0);
const juce::KeyPress kRight(juce::KeyPress::rightKey, juce::ModifierKeys::noModifiers, 0);
const juce::KeyPress kLeft(juce::KeyPress::leftKey, juce::ModifierKeys::noModifiers, 0);
const juce::KeyPress kAltRight(juce::KeyPress::rightKey, juce::ModifierKeys::altModifier, 0);
const juce::KeyPress kAltLeft(juce::KeyPress::leftKey, juce::ModifierKeys::altModifier, 0);

// One arrow step from the focused stop the way a user does it: a button never uses Left/Right, so the plain arrow steps
// along; every other control (slider, number field, combo box, segmented switch, text box) keeps the plain arrows for
// its own value, so the Alt chord steps along. False when focus did not move.
bool stepByArrow(synth::ui::EditorTabWalk& walk, juce::Component& editor, juce::Component*& focused, bool right) {
    auto* from = focused;
    const bool plain = dynamic_cast<juce::Button*>(from) != nullptr;
    deliver(walk, editor, *from, plain ? (right ? kRight : kLeft) : (right ? kAltRight : kAltLeft));
    return focused != from;
}

} // namespace

TEST(EditorHeaderTabReach, EveryHeaderControlOfEveryClipEditorIsANamedTabStopFromItsGrid) {
    for (const auto& row : editors()) {
        SCOPED_TRACE(row.name);
        PanelWithClip f;
        auto& editor = row.open(f);
        f.panel.resized();
        ASSERT_TRUE(f.panel.clipEditorOpen());
        auto& walk = row.walk(f);

        std::vector<juce::Component*> header;
        collectHeaderControls(editor, editor, row.headerHeight, header);
        ASSERT_FALSE(header.empty()) << "the walk is not seeing this header";
        std::stable_sort(header.begin(), header.end(), [&](auto* a, auto* b) {
            return editor.getLocalPoint(a->getParentComponent(), a->getPosition()).x <
                   editor.getLocalPoint(b->getParentComponent(), b->getPosition()).x;
        });

        juce::Component* focused = &editor;
        std::vector<juce::Component*> landed;
        walk.setFocusHooksForTest({[&] { return focused; },
                                   [&](juce::Component& to) {
                                       focused = &to;
                                       landed.push_back(&to);
                                   }});

        // Forward: from the grid round to the grid, then the next Tab leaves.
        for (int guard = 0; guard < 64; ++guard) {
            ASSERT_TRUE(deliver(walk, editor, *focused, kTab))
                << "Tab from " << focused->getComponentID() << " was not claimed";
            if (focused == &editor)
                break;
        }
        ASSERT_EQ(focused, &editor) << "the walk comes back to the grid";
        EXPECT_FALSE(deliver(walk, editor, *focused, kTab)) << "the Tab after a full walk leaves for the region cycle";

        std::vector<juce::Component*> forward(landed.begin(), landed.end() - 1);
        size_t previous = 0;
        for (auto* control : header) {
            SCOPED_TRACE(control->getComponentID().toStdString());
            const auto at = std::find(forward.begin(), forward.end(), control);
            ASSERT_NE(at, forward.end()) << "a header control Tab never reaches";
            EXPECT_GE((size_t)(at - forward.begin()), previous) << "header stops are walked left to right";
            previous = (size_t)(at - forward.begin());
            EXPECT_TRUE(control->getWantsKeyboardFocus());
            EXPECT_FALSE(nameOf(*control).empty()) << "a screen reader has a name to read";
            const auto* tip = dynamic_cast<juce::TooltipClient*>(control);
            ASSERT_NE(tip, nullptr);
            EXPECT_TRUE(const_cast<juce::TooltipClient*>(tip)->getTooltip().isNotEmpty()) << "a tooltip";
        }

        // Backward: the same stops in the other order.
        landed.clear();
        focused = &editor;
        for (int guard = 0; guard < 64; ++guard) {
            ASSERT_TRUE(deliver(walk, editor, *focused, kShiftTab));
            if (focused == &editor)
                break;
        }
        std::vector<juce::Component*> backward(landed.begin(), landed.end() - 1);
        std::reverse(backward.begin(), backward.end());
        EXPECT_EQ(backward, forward) << "Shift+Tab walks the same stops backwards";
    }
}

// Regression test for FRO1549: the drum machine's and the piano roll's header took Tab only, unlike the Timeline's and
// the Mixer's top bars, which take Left and Right; keyboard-reach.
TEST(EditorHeaderTabReach, LeftAndRightWalkEveryHeaderStopOfEveryClipEditorAndTheEndsReturnToTheGrid) {
    for (const auto& row : editors()) {
        SCOPED_TRACE(row.name);
        PanelWithClip f;
        auto& editor = row.open(f);
        f.panel.resized();
        auto& walk = row.walk(f);

        juce::Component* focused = &editor;
        std::vector<juce::Component*> landed;
        walk.setFocusHooksForTest({[&] { return focused; },
                                   [&](juce::Component& to) {
                                       focused = &to;
                                       landed.push_back(&to);
                                   }});

        // The stops Tab reaches, as the reference order.
        const auto stops = walk.getStops();
        ASSERT_GE(stops.size(), 3u);

        // The grid keeps its own arrows: Right on the grid is not claimed.
        deliver(walk, editor, *focused, kRight);
        EXPECT_EQ(focused, &editor);

        ASSERT_TRUE(deliver(walk, editor, *focused, kTab));
        ASSERT_EQ(focused, stops.front());
        landed.clear();
        for (size_t i = 1; i < stops.size(); ++i) {
            ASSERT_TRUE(stepByArrow(walk, editor, focused, true)) << "Right from " << nameOf(*stops[i - 1]);
            EXPECT_EQ(focused, stops[i]) << "Right steps to the next stop";
        }
        EXPECT_TRUE(stepByArrow(walk, editor, focused, true)) << "Right from the last stop";
        EXPECT_EQ(focused, &editor) << "past the last stop Right hands focus back to the grid";

        // Back in, then walk left to the first stop and out again.
        ASSERT_TRUE(deliver(walk, editor, *focused, kTab)) << "an arrow back to the grid leaves the Tab walk ready";
        ASSERT_EQ(focused, stops.front());
        for (size_t i = 1; i < stops.size(); ++i)
            ASSERT_TRUE(stepByArrow(walk, editor, focused, true));
        for (size_t i = stops.size() - 1; i > 0; --i) {
            ASSERT_TRUE(stepByArrow(walk, editor, focused, false)) << "Left from " << nameOf(*stops[i]);
            EXPECT_EQ(focused, stops[i - 1]) << "Left steps to the previous stop";
        }
        EXPECT_TRUE(stepByArrow(walk, editor, focused, false));
        EXPECT_EQ(focused, &editor) << "before the first stop Left hands focus back to the grid";
    }
}
