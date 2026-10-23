// Up and Down inside the Sounds view reach the Filters button, the chip row and the open panel's controls and hand off
// to the list, delivered through the order a real key takes (a component's key listeners, then its keyPressed, then its
// parent) rather than by calling one control's keyPressed. Headless windows cannot hold keyboard focus, so where a step
// lands is read from lastFocusRequestForTest().
#include "../App/MainComponent/MainComponentTestFixture.h"
#include "../UI/Layout/NativeWindowTestGuard.h"
#include "SoundBrowserTestKit.h"
#include "UI/Layout/FocusStepWithin.h"
#include "UI/Library/SoundBrowser/SoundBrowserComponent.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <gtest/gtest.h>
#include <typeinfo>

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

namespace {

// Every focusable control of the view but the search box (the first stop, reached by Up from Filters).
std::vector<juce::Component*> controlsBesideTheSearch(SoundBrowserComponent& v) {
    auto all = synth::ui::focusableDescendants(v);
    all.erase(std::remove(all.begin(), all.end(), &v.getSearchField()), all.end());
    return all;
}

// A height at which the open panel's view shows the footer in `mode`, found by shrinking the view; 0 when none does.
int heightShowing(SoundBrowserComponent& v, SoundBrowserComponent::FooterMode mode) {
    for (int h = 900; h >= 120; h -= 5) {
        v.setSize(300, h);
        v.completeFilterMotionForTest();
        if (v.getFooterMode() == mode)
            return h;
    }
    return 0;
}

// Walks Down from Filters through the header, the list and the footer using only dispatched keys, then Up back to the
// search, and checks that every control of the view was visited once each way.
void walkTheWholeView(Fixture& f, const char* what) {
    auto& v = *f.view;
    SCOPED_TRACE(what);
    const auto expected = controlsBesideTheSearch(v);
    ASSERT_GE(expected.size(), 8u);
    for (auto* control : expected)
        EXPECT_TRUE(v.listensForStepKeysForTest(*control)) << "no stepping on: " << control->getTitle();

    const auto press = [&](juce::Component& from, int code) {
        EXPECT_TRUE(dispatchKey(v, from, key(code))) << from.getTitle();
        return f.landed();
    };
    const auto isFooter = [&](juce::Component* c) { return c != nullptr && v.getPreviewFooter().isParentOf(c); };
    const size_t listSteps = v.getRows().size() + 2;

    std::vector<juce::Component*> down{&v.getFiltersButton()};
    auto* current = down.front();
    for (int guard = 0; guard < 50 && current != &v; ++guard) { // the header, then the list
        current = press(*current, juce::KeyPress::downKey);
        down.push_back(current);
    }
    ASSERT_EQ(current, &v) << "Down off the last header stop lands on the list";
    for (size_t i = 0; i < listSteps && current == &v; ++i) { // rows, then the footer when there is one
        dispatchKey(v, v, key(juce::KeyPress::downKey));
        current = f.landed();
    }
    const bool hasFooter = current != &v;
    for (int guard = 0; hasFooter && guard < 20; ++guard) {
        down.push_back(current);
        auto* next = press(*current, juce::KeyPress::downKey);
        if (next == current)
            break; // Down past the last footer stop is consumed and goes nowhere
        current = next;
    }
    EXPECT_EQ(hasFooter, v.getPreviewFooter().isVisible());
    for (auto* control : expected)
        EXPECT_NE(std::find(down.begin(), down.end(), control), down.end())
            << "Down never reached " << control->getTitle();

    std::vector<juce::Component*> up;
    for (int guard = 0; guard < 20 && isFooter(current); ++guard) { // back up the footer to the list
        current = press(*current, juce::KeyPress::upKey);
        up.push_back(current);
    }
    ASSERT_EQ(current, &v);
    for (size_t i = 0; i < listSteps && current == &v; ++i) { // up the rows to the first, then off it
        dispatchKey(v, v, key(juce::KeyPress::upKey));
        current = f.landed();
    }
    for (int guard = 0; guard < 50 && current != &v.getSearchField(); ++guard) {
        up.push_back(current);
        current = press(*current, juce::KeyPress::upKey);
    }
    EXPECT_EQ(current, &v.getSearchField()) << "Up from Filters returns to the search";
    for (auto* control : expected)
        if (!isFooter(control))
            EXPECT_NE(std::find(up.begin(), up.end(), control), up.end()) << "Up never reached " << control->getTitle();
}

} // namespace

TEST(SoundBrowserKeyboardReachTest, DownAndUpVisitEveryControlOfTheViewAtEveryFooterSize) {
    Fixture f;
    auto& v = *f.view;
    v.setSongKey("D minor");
    f.switchOneFilterOn();
    ASSERT_TRUE(v.areFiltersOpen());
    ASSERT_TRUE(v.getChipRow().isVisible());
    const auto panelStops = synth::ui::focusableDescendants(v.getFilterPanel());
    ASSERT_NE(std::find(panelStops.begin(), panelStops.end(), &v.getFilterPanel().songKeyGroup()), panelStops.end())
        << "the Song key chip shows";

    using Mode = SoundBrowserComponent::FooterMode;
    const int tall = heightShowing(v, Mode::Full);
    const int compact = heightShowing(v, Mode::Compact);
    const int hidden = heightShowing(v, Mode::Hidden);
    ASSERT_GT(tall, 0);
    ASSERT_GT(compact, 0);
    for (const auto& [h, name] :
         {std::pair{tall, "full footer"}, std::pair{compact, "compact footer"}, std::pair{hidden, "no footer"}}) {
        if (h == 0)
            continue;
        v.setSize(300, h);
        f.settle();
        v.selectRow(0);
        walkTheWholeView(f, name);
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
        if (hidden(*stops[i]))
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

TEST(SoundBrowserKeyboardReachTest, AMenuStepsWithUpAndDownAndOpensWithReturnOrSpace) {
    Fixture f;
    auto& v = *f.view;
    v.setFiltersOpen(true, false);
    f.settle();
    auto& panel = v.getFilterPanel();
    auto& length = panel.lengthBox();
    length.setSelectedId(2, juce::dontSendNotification);
    const int chosen = length.getSelectedId();

    EXPECT_TRUE(dispatchKey(v, length, key(juce::KeyPress::downKey)));
    EXPECT_EQ(f.landed(), &panel.placeBox());
    EXPECT_TRUE(dispatchKey(v, length, key(juce::KeyPress::upKey)));
    EXPECT_EQ(f.landed(), &panel.keyBox());
    EXPECT_EQ(length.getSelectedId(), chosen) << "Up and Down no longer change a menu";

    // A popup menu places itself on a display; a headless CI runner has none, and opening one there crashes.
    if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr)
        GTEST_SKIP() << "no display to open a menu on";
    EXPECT_FALSE(length.isPopupActive());
    EXPECT_TRUE(dispatchKey(v, length, key(juce::KeyPress::returnKey)));
    EXPECT_TRUE(length.isPopupActive());
    length.hidePopup();
    EXPECT_FALSE(length.isPopupActive());
    EXPECT_TRUE(dispatchKey(v, length, space()));
    EXPECT_TRUE(length.isPopupActive());
    length.hidePopup();
}

TEST(SoundBrowserKeyboardReachTest, LeftAndRightStillMoveATempoHandleAndTheVolume) {
    Fixture f;
    auto& v = *f.view;
    v.setFiltersOpen(true, false);
    f.settle();
    auto& tempo = v.getFilterPanel().tempoSlider();
    tempo.setValues(100.0, 140.0);
    auto& lower = tempo.getHandle(synth::ui::RangeSlider::Handle::Lower);
    EXPECT_TRUE(dispatchKey(v, lower, key(juce::KeyPress::rightKey)));
    EXPECT_GT(tempo.getLower(), 100.0);
    EXPECT_TRUE(dispatchKey(v, lower, key(juce::KeyPress::leftKey)));
    EXPECT_DOUBLE_EQ(tempo.getLower(), 100.0);
    EXPECT_TRUE(dispatchKey(v, lower, key(juce::KeyPress::downKey)));
    EXPECT_DOUBLE_EQ(tempo.getLower(), 100.0) << "Down steps focus, it does not move the handle";

    auto& volume = v.getPreviewFooter().getVolume();
    const double before = volume.getValue();
    EXPECT_TRUE(dispatchKey(v, volume, key(juce::KeyPress::rightKey)));
    EXPECT_GT(volume.getValue(), before);
    EXPECT_TRUE(dispatchKey(v, volume, key(juce::KeyPress::leftKey)));
    EXPECT_DOUBLE_EQ(volume.getValue(), before);
}

TEST(SoundBrowserKeyboardReachTest, DownFromTheLastRowEntersTheFooterAndUpFromItsFirstControlReturns) {
    Fixture f;
    auto& v = *f.view;
    ASSERT_EQ(v.getFooterMode(), SoundBrowserComponent::FooterMode::Full);
    int last = -1;
    for (int i = 0; i < (int)v.getRows().size(); ++i)
        last = v.getRows()[(size_t)i].navigable() ? i : last;
    v.selectRow(last);
    EXPECT_TRUE(dispatchKey(v, v, key(juce::KeyPress::downKey)));
    EXPECT_EQ(f.landed(), &v.getPreviewFooter().getStopButton());
    EXPECT_EQ(v.getSelectedIndex(), last) << "the selection is kept";
    EXPECT_TRUE(dispatchKey(v, v.getPreviewFooter().getStopButton(), key(juce::KeyPress::upKey)));
    EXPECT_EQ(f.landed(), &v);
    EXPECT_EQ(v.getSelectedIndex(), last);
}

TEST(SoundBrowserKeyboardReachTest, ACompactFooterIsReachableAndAHiddenOneMakesDownOnTheLastRowANoOp) {
    Fixture f;
    auto& v = *f.view;
    using Mode = SoundBrowserComponent::FooterMode;
    ASSERT_GT(heightShowing(v, Mode::Compact), 0);
    v.selectRow((int)v.getRows().size() - 1);
    EXPECT_TRUE(dispatchKey(v, v, key(juce::KeyPress::downKey)));
    EXPECT_EQ(f.landed(), &v.getPreviewFooter().getStopButton());

    const int hiddenHeight = heightShowing(v, Mode::Hidden);
    ASSERT_GT(hiddenHeight, 0);
    ASSERT_FALSE(v.getPreviewFooter().isVisible());
    v.selectRow((int)v.getRows().size() - 1);
    const auto* before = f.landed();
    dispatchKey(v, v, key(juce::KeyPress::downKey));
    EXPECT_EQ(f.landed(), before) << "no footer, no stop";
}

TEST(SoundBrowserKeyboardReachTest, TheFooterStopsAreItsControlsNotTheVolumeTextBox) {
    Fixture f;
    auto& footer = f.view->getPreviewFooter();
    const auto stops = synth::ui::focusableDescendants(footer);
    ASSERT_FALSE(stops.empty());
    EXPECT_EQ(stops.back(), &footer.getVolume()) << "the volume slider itself is the last footer stop";
    for (auto* stop : stops)
        EXPECT_EQ(stop->getParentComponent(), &footer) << "a stop inside a control: " << typeid(*stop).name();
}

// Regression test for FRO991: quitting with a footer control focused crashed in the view's own focusGained.
TEST(SoundBrowserKeyboardReachTest, DestroyingTheViewWithAFooterControlFocusedNeverHandsItTheKeyboard) {
    if (nativeWindowsAbortHere() || juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr)
        GTEST_SKIP() << "needs a display for a native window";
    Fixture f;
    f.view->addToDesktop(juce::ComponentPeer::windowIsTemporary);
    f.view->setVisible(true);
    auto& stop = *synth::ui::focusableDescendants(f.view->getPreviewFooter()).front();
    stop.grabKeyboardFocus();
    if (!stop.hasKeyboardFocus(false))
        GTEST_SKIP() << "this machine would not give a temporary window the keyboard";
    const int before = SoundBrowserComponent::focusGainsWhileTearingDownForTest();
    f.view.reset();
    EXPECT_EQ(SoundBrowserComponent::focusGainsWhileTearingDownForTest(), before);
}
