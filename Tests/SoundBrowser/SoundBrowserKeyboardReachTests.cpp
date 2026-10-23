// Up and Down inside the Sounds view reach the Filters button, the chip row and the open panel's controls and hand off
// to the list, delivered through the order a real key takes (a component's key listeners, then its keyPressed, then its
// parent) rather than by calling one control's keyPressed. Headless windows cannot hold keyboard focus, so where a step
// lands is read from lastFocusRequestForTest().
#include "../App/MainComponent/MainComponentTestFixture.h"
#include "SoundBrowserTestKit.h"
#include "UI/Layout/FocusStepWithin.h"
#include "UI/Library/SoundBrowser/SoundBrowserComponent.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <gtest/gtest.h>

using namespace sound_browser_test;
using synth::ui::SoundBrowserComponent;

namespace {

juce::KeyPress key(int code) { return juce::KeyPress(code, juce::ModifierKeys(), 0); }
juce::KeyPress space() { return juce::KeyPress(juce::KeyPress::spaceKey, juce::ModifierKeys(), ' '); }

// The order of juce::ComponentPeer::handleKeyPress (JUCE 8): from the focused component up through its parents, each
// one offers the key to its key listeners first and to its own keyPressed second, and the first to use it ends the
// walk.
bool dispatchKey(SoundBrowserComponent& view, juce::Component& origin, const juce::KeyPress& press) {
    for (auto* target = &origin; target != nullptr; target = target->getParentComponent()) {
        if (view.offerStepKeyForTest(press, *target))
            return true;
        if (target->keyPressed(press))
            return true;
    }
    return false;
}

bool usesArrows(const juce::Component& c) {
    return dynamic_cast<const juce::ComboBox*>(&c) != nullptr ||
           dynamic_cast<const synth::ui::RangeSlider*>(c.getParentComponent()) != nullptr;
}

struct Fixture {
    TempTree tree;
    synth::soundbrowser::SoundLists lists;
    std::unique_ptr<synth::soundbrowser::SoundIndex> index;
    std::unique_ptr<SoundBrowserComponent> view;
    explicit Fixture(int height = 900) {
        tree.fill();
        index = makeIndex(tree);
        index->setUserFolders({tree.user});
        scanAndWait(*index);
        view = std::make_unique<SoundBrowserComponent>(*index, lists);
        view->setSize(300, height);
        view->setAnimateOffscreenForTest(true);
    }
    void settle() {
        view->completeFilterMotionForTest();
        view->completeListAnimationForTest();
    }
    void switchOneFilterOn() {
        view->setFiltersOpen(true, false);
        settle();
        auto& kind = view->getFilterPanel().kindGroup();
        kind.setFocusedIndex(0);
        EXPECT_TRUE(kind.keyPressed(space()));
        settle();
    }
    juce::Component* landed() const { return view->lastFocusRequestForTest(); }
};

} // namespace

TEST(SoundBrowserKeyboardReachTest, WithAChipTheStepsRunSearchFiltersChipsListAndBack) {
    Fixture f;
    auto& v = *f.view;
    f.switchOneFilterOn();
    v.setFiltersOpen(false, false);
    f.settle();
    ASSERT_TRUE(v.getChipRow().isVisible());

    EXPECT_TRUE(dispatchKey(v, v.getFiltersButton(), key(juce::KeyPress::downKey)));
    EXPECT_EQ(f.landed(), &v.getChipRow());
    EXPECT_TRUE(dispatchKey(v, v.getChipRow(), key(juce::KeyPress::downKey)));
    EXPECT_EQ(f.landed(), &v);
    ASSERT_GE(v.getSelectedIndex(), 0) << "the list lands on its first row";
    EXPECT_EQ(v.getRows()[(size_t)v.getSelectedIndex()].kind, SoundBrowserComponent::RowKind::Sound);

    EXPECT_TRUE(dispatchKey(v, v, key(juce::KeyPress::upKey)));
    EXPECT_EQ(f.landed(), &v.getChipRow());
    EXPECT_TRUE(dispatchKey(v, v.getChipRow(), key(juce::KeyPress::upKey)));
    EXPECT_EQ(f.landed(), &v.getFiltersButton());
    EXPECT_TRUE(dispatchKey(v, v.getFiltersButton(), key(juce::KeyPress::upKey)));
    EXPECT_EQ(f.landed(), &v.getSearchField());
}

TEST(SoundBrowserKeyboardReachTest, WithoutChipsFiltersAndTheListAreNeighbours) {
    Fixture f;
    auto& v = *f.view;
    ASSERT_FALSE(v.getChipRow().isVisible());
    EXPECT_TRUE(dispatchKey(v, v.getFiltersButton(), key(juce::KeyPress::downKey)));
    EXPECT_EQ(f.landed(), &v);
    v.selectRow(0);
    EXPECT_TRUE(dispatchKey(v, v, key(juce::KeyPress::upKey)));
    EXPECT_EQ(f.landed(), &v.getFiltersButton());
}

TEST(SoundBrowserKeyboardReachTest, UpInTheMiddleOfTheListStillMovesTheSelection) {
    Fixture f;
    auto& v = *f.view;
    ASSERT_GE(v.getRows().size(), 2u);
    v.selectRow(1);
    EXPECT_TRUE(dispatchKey(v, v, key(juce::KeyPress::upKey)));
    EXPECT_EQ(v.getSelectedIndex(), 0);
    EXPECT_EQ(f.landed(), nullptr);
}

TEST(SoundBrowserKeyboardReachTest, DownFromTheSearchStillGoesToTheList) {
    Fixture f;
    auto& v = *f.view;
    EXPECT_TRUE(dispatchKey(v, v.getSearchField(), key(juce::KeyPress::downKey)));
    EXPECT_GE(v.getSelectedIndex(), 0);
    EXPECT_EQ(f.landed(), nullptr) << "no new stepping from the search box";
}

TEST(SoundBrowserKeyboardReachTest, EveryControlOfTheViewIsReachableAndTheWalkVisitsThemInOrder) {
    Fixture f;
    auto& v = *f.view;
    f.switchOneFilterOn();
    ASSERT_TRUE(v.areFiltersOpen());
    ASSERT_TRUE(v.getChipRow().isVisible());

    std::vector<juce::Component*> expected{&v.getFiltersButton(), &v.getChipRow()};
    for (auto* control : synth::ui::focusableDescendants(v.getFilterPanel()))
        expected.push_back(control);

    for (auto* control : synth::ui::focusableDescendants(v)) {
        if (control == &v.getSearchField() || v.getPreviewFooter().isParentOf(control))
            continue; // the search box is the first stop; the footer is not part of the header
        EXPECT_NE(std::find(expected.begin(), expected.end(), control), expected.end())
            << "unreachable by Up and Down: " << control->getTitle();
        EXPECT_TRUE(v.listensForStepKeysForTest(*control)) << "no stepping on: " << control->getTitle();
    }

    ASSERT_GE(expected.size(), 6u);
    for (size_t i = 0; i < expected.size(); ++i) {
        auto& current = *expected[i];
        if (usesArrows(current)) {
            EXPECT_FALSE(v.offerStepKeyForTest(key(juce::KeyPress::downKey), current))
                << current.getTitle() << " keeps its own arrows";
            EXPECT_FALSE(v.offerStepKeyForTest(key(juce::KeyPress::upKey), current));
            continue;
        }
        EXPECT_TRUE(dispatchKey(v, current, key(juce::KeyPress::downKey))) << current.getTitle();
        EXPECT_EQ(f.landed(), i + 1 < expected.size() ? expected[i + 1] : &v) << "from " << current.getTitle();
    }
}

TEST(SoundBrowserKeyboardReachTest, UpFromTheTopRowLandsOnTheLastPanelControlWhileThePanelIsOpen) {
    Fixture f;
    auto& v = *f.view;
    v.setFiltersOpen(true, false);
    f.settle();
    v.selectRow(0);
    EXPECT_TRUE(dispatchKey(v, v, key(juce::KeyPress::upKey)));
    EXPECT_EQ(f.landed(), synth::ui::focusableDescendants(v.getFilterPanel()).back());
}

TEST(SoundBrowserKeyboardReachTest, ASteppedToControlBelowTheCappedPanelScrollsIntoView) {
    Fixture f(260);
    auto& v = *f.view;
    v.setFiltersOpen(true, false);
    f.settle();
    auto& host = v.getPanelViewport();
    auto& panel = v.getFilterPanel();
    const auto stops = synth::ui::focusableDescendants(panel);
    host.setViewPosition(0, 0);
    auto hidden = [&](juce::Component& c) {
        return panel.getLocalArea(&c, c.getLocalBounds()).getBottom() > host.getMaximumVisibleHeight();
    };
    size_t target = 0;
    for (size_t i = 1; i < stops.size() && target == 0; ++i)
        if (hidden(*stops[i]) && !usesArrows(*stops[i - 1]))
            target = i;
    ASSERT_GT(target, 0u) << "a short column hides a control that a Down step reaches";

    EXPECT_TRUE(dispatchKey(v, *stops[target - 1], key(juce::KeyPress::downKey)));
    EXPECT_EQ(f.landed(), stops[target]);
    const auto box = panel.getLocalArea(stops[target], stops[target]->getLocalBounds());
    EXPECT_GT(host.getViewPositionY(), 0);
    EXPECT_LE(box.getBottom(), host.getViewPositionY() + host.getMaximumVisibleHeight());
}

TEST(SoundBrowserKeyboardReachTest, EscFromAMenuInThePanelStillClosesIt) {
    Fixture f;
    auto& v = *f.view;
    v.setFiltersOpen(true, false);
    f.settle();
    auto& combo = v.getFilterPanel().lengthBox();
    EXPECT_TRUE(dispatchKey(v, combo, key(juce::KeyPress::escapeKey)));
    f.settle();
    EXPECT_FALSE(v.areFiltersOpen());
}
