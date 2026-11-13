// PopupOwnPathTests.cpp -- a popover opens through one path, Layout/PopupHost (launchPopover, PopoverSlot). Opening a
// juce::CallOutBox any other way brings back the faults that path settles: two engines animating one window (pops),
// a click on the opener's button that closes the popover and re-opens it (flicker), and an Escape that is handled
// differently by every content. This scans Source for a raw call-out launch, lists every file that opens a popover
// (PopoverRegistry.h), and checks that no pop-up menu is given a look and feel of its own.
#include "../SourceScan.h"
#include "PopoverRegistry.h"
#include <gtest/gtest.h>
#include <map>
#include <regex>
#include <set>
#include <string>

using synth::test::hoverLabelPainters;
using synth::test::popoverUsers;
using synth::test::source_scan::countCallSites;
using synth::test::source_scan::expectCountsMatch;
using synth::test::source_scan::sourceTexts;

namespace {

bool contains(const std::string& text, const std::regex& pattern) { return std::regex_search(text, pattern); }

} // namespace

// Outside Layout/PopupHost.*, no file builds or launches a call-out itself, and nothing refers to the removed reveal.
// The help popover is the one exception: its content is the same panel the user can pin as a floating window, so it
// moves between the box and the pin and cannot be owned by a launched box.
TEST(PopupOwnPath, NoFileOpensACallOutOutsidePopupHost) {
    const std::map<std::string, int> allowedDirectBuilds = {
        {"Source/UI/Library/ModuleLibraryComponent/ModuleLibraryHelpPopover.cpp", 1}};
    const std::string skip = "Source/UI/Layout/PopupHost.";

    const auto launches = countCallSites(std::regex(R"(CallOutBox::launch(?:Asynchronously)?\b)"), skip);
    EXPECT_TRUE(launches.empty()) << "a CallOutBox::launch call outside PopupHost: use launchPopover (first: "
                                  << (launches.empty() ? "" : launches.begin()->first) << ")";

    expectCountsMatch(
        "direct CallOutBox construction",
        countCallSites(std::regex(R"(new\s+juce::CallOutBox\b|make_unique<\s*juce::CallOutBox\s*>)"), skip),
        allowedDirectBuilds, "PopupOwnPathTests.cpp (allowedDirectBuilds)");

    const auto reveals = countCallSites(std::regex(R"(\bCalloutReveal\b)"), "");
    EXPECT_TRUE(reveals.empty()) << "CalloutReveal is gone, PopupMotion animates every call-out (first: "
                                 << (reveals.empty() ? "" : reveals.begin()->first) << ")";
}

// Every file that calls launchPopover( or declares a PopoverSlot is in PopoverRegistry.h with the content classes it
// opens, so PopupHostBehaviourTests builds each one. A file that does neither any more must leave the registry.
TEST(PopupOwnPath, EveryPopoverOpenerIsRegistered) {
    const std::regex opens(R"(\blaunchPopover\s*\(|\bPopoverSlot\b)");
    std::set<std::string> found;
    for (const auto& [file, text] : sourceTexts())
        if (file.rfind("Source/UI/Layout/PopupHost.", 0) != 0 && contains(text, opens))
            found.insert(file);
    ASSERT_GT(found.size(), 20u) << "the scan found almost nothing: has launchPopover been renamed?";

    std::set<std::string> registered;
    for (const auto& user : popoverUsers()) {
        EXPECT_TRUE(registered.insert(user.file).second) << user.file << " is registered twice";
        EXPECT_FALSE(user.contents.empty()) << user.file << ": name the content class it opens";
    }
    for (const auto& file : found)
        EXPECT_TRUE(registered.count(file) > 0)
            << "not registered: " << file << " opens a popover - add it to PopoverRegistry.h with its content class";
    for (const auto& file : registered)
        EXPECT_TRUE(found.count(file) > 0)
            << "stale: " << file << " no longer calls launchPopover or declares a PopoverSlot - remove it from "
            << "PopoverRegistry.h";
}

// What this checks about pop-up menus: every menu takes its look and feel from the component tree it opens in, and
// that is the app's (AppLookAndFeel, which hands the menu window to PopupMotion) because the standalone app installs
// it as the desktop default in Main.cpp and a plugin editor installs it on its editor. A menu is only left out if
// something installs another look and feel: another desktop default, or a look and feel set on a menu itself.
TEST(PopupOwnPath, NoPopupMenuIsGivenALookAndFeelOfItsOwn) {
    const auto defaults = countCallSites(std::regex(R"(setDefaultLookAndFeel\s*\(\s*(?!nullptr))"), "");
    EXPECT_EQ(defaults.size(), 1u) << "only the standalone app's Main.cpp sets the desktop default look and feel";
    EXPECT_TRUE(defaults.count("Source/Main.cpp") > 0);

    const auto onMenus =
        countCallSites(std::regex(R"(\b\w*(?:[mM]enu|PopupMenu)\w*\s*(?:\.|->)\s*setLookAndFeel\s*\()"), "");
    for (const auto& [file, n] : onMenus)
        ADD_FAILURE() << file << " sets a look and feel on a pop-up menu (" << n
                      << "): it would skip PopupMotion's hook and show without the app's fade";
}

// A hover label painted straight onto a card (the mod chip under a knob, drawn in paintOverChildren) has no window, so
// the CallOutBox scans above never see it. Every file outside Theme/ that draws the shared tooltip box
// (paintTooltipBox, drawTooltip) is registered with the object that owns its alpha, and its file must name that owner:
// a new painted label with an alpha of its own fails by name (the chip under a knob once popped because nothing faded
// it).
TEST(PopupOwnPath, EveryPaintedHoverLabelHasAFadeOwner) {
    const auto drawn = countCallSites(std::regex(R"(\b(?:paintTooltipBox|drawTooltip)\s*\()"), "Source/UI/Theme/");
    ASSERT_GE(drawn.size(), 1u) << "the scan found nothing: have paintTooltipBox and drawTooltip been renamed?";

    const auto texts = sourceTexts();
    std::set<std::string> registered;
    for (const auto& painter : hoverLabelPainters()) {
        EXPECT_TRUE(registered.insert(painter.file).second) << painter.file << " is registered twice";
        const auto text = texts.find(painter.file);
        ASSERT_NE(text, texts.end()) << "stale: " << painter.file << " does not exist";
        EXPECT_NE(text->second.find(painter.fadeOwner), std::string::npos)
            << painter.file << " paints a hover label but no longer names its fade owner " << painter.fadeOwner;
    }
    for (const auto& [file, n] : drawn)
        EXPECT_TRUE(registered.count(file) > 0)
            << "not registered: " << file << " draws a tooltip box itself - fade it with a ShownFade and add it to "
            << "hoverLabelPainters() in PopoverRegistry.h";
    for (const auto& file : registered)
        EXPECT_TRUE(drawn.count(file) > 0) << "stale: " << file << " no longer draws a tooltip box - remove it";
}
