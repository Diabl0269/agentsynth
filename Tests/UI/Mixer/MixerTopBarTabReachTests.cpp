// MixerTopBarTabReachTests.cpp -- the mixer's toolbar strip is reachable with Tab. The app's Tab is the region cycle,
// so the panel claims it from its root with an EditorTabWalk: Tab walks the toolbar buttons, the side pane's filter and
// chips and the pan law button, comes back to the panel, and the next Tab leaves for the next region.
#include "../Layout/KeyboardReachFixture.h"
#include "UI/Layout/EditorTabWalk.h"
#include "UI/Mixer/MixerPanelComponent/MixerPanelComponent.h"
#include <algorithm>
#include <vector>

namespace {

using Tab = synth::ui::BottomDockComponent::Tab;

class MixerTopBarTabReach : public KeyboardReachFixture {};

// The key travels as juce::ComponentPeer::handleKeyPress sends it: from the focused control up through its parents,
// each one's key listeners before its own keyPressed, stopping at the panel.
bool deliver(synth::ui::EditorTabWalk& walk, juce::Component& panel, juce::Component& focused,
             const juce::KeyPress& key) {
    for (auto* target = &focused; target != nullptr; target = target->getParentComponent()) {
        if ((target == &panel || walk.isWatching(*target)) && walk.keyPressed(key, target))
            return true;
        if (target->keyPressed(key))
            return true;
        if (target == &panel)
            return false;
    }
    return false;
}

const juce::KeyPress kTab(juce::KeyPress::tabKey, juce::ModifierKeys::noModifiers, 0);
const juce::KeyPress kShiftTab(juce::KeyPress::tabKey, juce::ModifierKeys::shiftModifier, 0);

} // namespace

// Regression test for FRO2233: the mixer's toolbar strip (Inserts, Sends, EQ, Reset Meters, Add Bus) could not be
// reached with Tab; only the top-bar ring's own key reached it.
TEST_F(MixerTopBarTabReach, TabFromTheMixerWalksTheToolbarStripAndComesBackToThePanel) {
    mc_->setMainViewForTest(Tab::Mixer);
    mc_->resized();
    auto& panel = mc_->getBottomDock().getMixerPanel();
    auto& toolbar = panel.getToolbarForTest();
    auto& walk = panel.getTabWalkForTest();

    juce::Component* focused = &panel;
    std::vector<juce::Component*> landed;
    walk.setFocusHooksForTest({[&] { return focused; },
                               [&](juce::Component& to) {
                                   focused = &to;
                                   landed.push_back(&to);
                               }});

    for (int guard = 0; guard < 64; ++guard) {
        ASSERT_TRUE(deliver(walk, panel, *focused, kTab)) << "Tab was not claimed from " << focused->getTitle();
        if (focused == &panel)
            break;
    }
    ASSERT_EQ(focused, &panel) << "the walk comes back to the panel";
    EXPECT_FALSE(deliver(walk, panel, *focused, kTab)) << "the Tab after a full walk leaves for the region cycle";

    std::vector<juce::Component*> forward(landed.begin(), landed.end() - 1);
    for (const auto section :
         {synth::ui::MixerSection::Inserts, synth::ui::MixerSection::Sends, synth::ui::MixerSection::Eq})
        EXPECT_NE(std::find(forward.begin(), forward.end(), &toolbar.getSectionToggle(section)), forward.end())
            << "a section toggle Tab never reaches";
    for (auto* button : {static_cast<juce::Component*>(&toolbar.getResetMetersButtonForTest()),
                         static_cast<juce::Component*>(&toolbar.getAddBusButtonForTest())})
        EXPECT_NE(std::find(forward.begin(), forward.end(), button), forward.end()) << button->getName();

    landed.clear();
    focused = &panel;
    for (int guard = 0; guard < 64; ++guard) {
        ASSERT_TRUE(deliver(walk, panel, *focused, kShiftTab));
        if (focused == &panel)
            break;
    }
    std::vector<juce::Component*> backward(landed.begin(), landed.end() - 1);
    std::reverse(backward.begin(), backward.end());
    EXPECT_EQ(backward, forward) << "Shift+Tab walks the same stops backwards";
}
