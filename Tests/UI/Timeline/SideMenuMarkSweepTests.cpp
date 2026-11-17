// SideMenuMarkSweepTests.cpp -- every control in Source/ that paints a menu mark (the shared side-menu notch, a combo
// chevron) is either an icon button with a side menu, which owns a SideMenuGesture and opens by right-click, corner
// press and held press, or is listed here with the reason it is not. A new control that paints a mark and is in
// neither list fails by name. docs/timeline/edit-tools.md#the-side-menu-notch
#include "../SourceScan.h"
#include "TimelinePanel/TimelinePanelTestEvents.h"
#include "UI/Chrome/ToolbarButton/SideMenuMark.h"
#include "UI/Chrome/ToolbarButton/SnapModeButton.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Timeline/AutomationLanes/LaneShapes/DrawPenButton.h"
#include "UI/Timeline/ClipSizingButton.h"
#include <functional>
#include <gtest/gtest.h>
#include <map>
#include <memory>
#include <string>

namespace {

// The files that paint a menu mark or own the gesture, with why each is on the list. "adopted" files are also
// exercised below.
const std::map<std::string, std::string>& registry() {
    static const std::map<std::string, std::string> entries = {
        {"Source/UI/Timeline/ClipSizingButton.h", "adopted: the Clip sizing chip"},
        {"Source/UI/Timeline/ClipSizingButton.cpp", "adopted: the Clip sizing chip"},
        {"Source/UI/Chrome/ToolbarButton/SnapModeButton.h", "adopted: the Snap mode picker"},
        {"Source/UI/Chrome/ToolbarButton/SnapModeButton.cpp", "adopted: the Snap mode picker"},
        {"Source/UI/Chrome/ToolbarButton/ToolbarMenuNotch.h",
         "exempt: declares the notch painter ClipSizingButton and SnapModeButton share (the buttons are listed)"},
        {"Source/UI/Chrome/ToolbarButton/ToolbarMenuNotch.cpp",
         "exempt: the notch painter ClipSizingButton and SnapModeButton share (the buttons are listed)"},
        {"Source/UI/Timeline/AutomationLanes/LaneShapes/DrawPenButton.h", "adopted: the pen"},
        {"Source/UI/Timeline/AutomationLanes/LaneShapes/DrawPenButton.cpp", "adopted: the pen"},
        {"Source/UI/Timeline/TimelineRoutingPane/TimelineRoutingPaneControls.cpp",
         "exempt: a text dropdown (caption and combo chevron), not an icon chip with a side menu"},
        {"Source/UI/Theme/AppLookAndFeel/AppLookAndFeel.h", "exempt: declares paintComboChevron for text combos"},
        {"Source/UI/Theme/AppLookAndFeel/AppLookAndFeelComboAndMenus.cpp",
         "exempt: draws every juce::ComboBox, a text dropdown whose whole face opens the menu"},
    };
    return entries;
}

// The helper itself is the mechanism, not a control.
constexpr const char* kHelperPrefix = "Source/UI/Chrome/ToolbarButton/SideMenuMark.";

} // namespace

TEST(SideMenuMarkSweep, EveryControlThatPaintsAMenuMarkIsAdoptedOrExemptWithAReason) {
    namespace scan = synth::test::source_scan;
    std::map<std::string, int> found;
    for (const auto& [file, n] : scan::countCallSites(
             std::regex(R"(paintComboChevron\(|paintSideMenuNotch\(|paintToolbarMenuNotch\(|\bSideMenuGesture\b)"),
             kHelperPrefix))
        found[file] = n;
    for (const auto& [file, n] : found)
        EXPECT_EQ(registry().count(file), 1u) << "not on the side-menu sweep list: " << file
                                              << " paints a menu mark. An icon button with a side menu uses "
                                                 "SideMenuGesture and paintSideMenuNotch; anything else gets an "
                                                 "'exempt: <reason>' entry in SideMenuMarkSweepTests.cpp";
    for (const auto& [file, why] : registry()) {
        EXPECT_EQ(found.count(file), 1u) << "stale entry (no menu mark painted there any more): " << file;
        EXPECT_TRUE(why.rfind("adopted:", 0) == 0 || why.rfind("exempt: ", 0) == 0 && why.size() > 12) << file;
    }
}

namespace {

// One adopted control as the sweep drives it: the component, its gesture and how many menus it has opened.
struct Adopted {
    std::string name;
    std::unique_ptr<juce::Component> component;
    synth::ui::SideMenuGesture* gesture = nullptr;
    int opened = 0;
    int picked = 0;            // a tool or mode change a menu press must never cause
    juce::Point<float> corner; // a point inside the mark's press corner
};

std::unique_ptr<Adopted> makeSizing() {
    auto a = std::make_unique<Adopted>();
    a->name = "Clip sizing chip";
    auto button = std::make_unique<synth::ui::ClipSizingButton>();
    button->setSize(button->getFixedWidth(), 28);
    button->showMenuHook = [raw = a.get()](juce::PopupMenu&) { ++raw->opened; };
    button->onModeChanged = [raw = a.get()](synth::ClipSizingMode) { ++raw->picked; };
    a->gesture = &button->getMenuGesture();
    const auto chip = synth::theme::toolbarChipBounds(*button);
    a->corner = {chip.getRight() - 3.0f, chip.getBottom() - 3.0f};
    a->component = std::move(button);
    return a;
}

std::unique_ptr<Adopted> makeSnapMode() {
    auto a = std::make_unique<Adopted>();
    a->name = "Snap mode picker";
    auto button = std::make_unique<synth::ui::SnapModeButton>("snapModeTest", "snapModeTest");
    button->setSize(button->getFixedWidth(), 28);
    button->showMenuHook = [raw = a.get()](juce::PopupMenu&) { ++raw->opened; };
    button->onModeChanged = [raw = a.get()](synth::ui::SnapMode) { ++raw->picked; };
    a->gesture = &button->getMenuGesture();
    const auto chip = synth::theme::toolbarChipBounds(*button);
    a->corner = {chip.getRight() - 3.0f, chip.getBottom() - 3.0f};
    a->component = std::move(button);
    return a;
}

std::unique_ptr<Adopted> makePen() {
    auto a = std::make_unique<Adopted>();
    a->name = "Draw pen";
    auto button = std::make_unique<synth::ui::DrawPenButton>("pen");
    button->setSize(24, 24);
    button->onOpenFlyout = [raw = a.get()] { ++raw->opened; };
    button->onClick = [raw = a.get()] { ++raw->picked; };
    a->gesture = &button->getMenuGesture();
    a->corner = {21.0f, 21.0f};
    a->component = std::move(button);
    return a;
}

const juce::ModifierKeys kLeft(juce::ModifierKeys::leftButtonModifier);
const juce::ModifierKeys kRight(juce::ModifierKeys::rightButtonModifier);

} // namespace

TEST(SideMenuMarkSweep, EveryAdoptedControlOpensItsMenuByRightClickCornerPressAndHold) {
    std::unique_ptr<Adopted> (*const makers[])() = {makeSizing, makeSnapMode, makePen};
    for (const auto make : makers) {
        {
            auto a = make();
            auto& c = *a->component;
            const auto centre = c.getLocalBounds().getCentre().toFloat();
            c.mouseDown(makeClickEvent(c, centre, kRight));
            c.mouseUp(makeClickEvent(c, centre, kRight));
            EXPECT_EQ(a->opened, 1) << a->name << ": right-click";
            EXPECT_EQ(a->picked, 0) << a->name << ": right-click picks nothing";
        }
        {
            auto a = make();
            auto& c = *a->component;
            const auto corner = a->corner;
            c.mouseDown(makeClickEvent(c, corner, kLeft));
            c.mouseUp(makeClickEvent(c, corner, kLeft));
            EXPECT_EQ(a->opened, 1) << a->name << ": corner press";
            EXPECT_EQ(a->picked, 0) << a->name << ": corner press picks nothing";
        }
        {
            auto a = make();
            auto& c = *a->component;
            const auto centre = c.getLocalBounds().getCentre().toFloat();
            c.mouseDown(makeClickEvent(c, centre, kLeft));
            EXPECT_TRUE(a->gesture->isHoldPendingForTest()) << a->name;
            a->gesture->holdElapsedForTest();
            EXPECT_EQ(a->opened, 1) << a->name << ": held press";
            c.mouseUp(makeClickEvent(c, centre, kLeft));
            EXPECT_EQ(a->opened, 1) << a->name << ": the release opens nothing more";
            EXPECT_EQ(a->picked, 0) << a->name << ": the release after a hold is not a click";
        }
    }
}
