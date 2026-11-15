// MenuPlacementRegistryTests.cpp -- where a menu opens: a menu a key opened anchors to the focused item, a menu the
// pointer opened opens at the pointer (docs/layout/card-popovers.md, "Menus"). Every showMenuAsync call in Source must
// build its options through contextMenuOptions* or name a target itself, so none falls back to a bare Options() that
// opens at the mouse whatever opened it; and a `showMenu` hook must not ignore the area it is given.
#include "../SourceScan.h"
#include <gtest/gtest.h>
#include <map>
#include <regex>
#include <string>

namespace {

// Files whose showMenuAsync call receives options built by its caller or a helper, which the scan cannot see from the
// call. This list may only shrink: never add a file here, build the options with contextMenuOptions* instead.
const std::map<std::string, int> kOptionsBuiltElsewhere = {
    {"Source/UI/Timeline/AutomationLanes/AutomationLaneHeader/AutomationLaneHeaderMenu.cpp", 1},
    {"Source/UI/Timeline/AutomationLanes/Modulators/ModulatorRowEdits.cpp", 1},
    {"Source/UI/Timeline/TimelineSectionStrip/TimelineSectionStripVariations.cpp", 1},
};

std::map<std::string, int> callsWithoutPlacement() {
    static const std::regex target("contextMenuOptions|withTargetComponent|withTargetScreenArea");
    std::map<std::string, int> found;
    for (const auto& [file, text] : synth::test::source_scan::sourceTexts()) {
        for (size_t at = text.find("showMenuAsync("); at != std::string::npos;
             at = text.find("showMenuAsync(", at + 1)) {
            size_t end = at + 14;
            for (int depth = 1; end < text.size() && depth > 0; ++end)
                depth += (text[end] == '(') - (text[end] == ')');
            const size_t from = at > 800 ? at - 800 : 0;
            const std::string window = text.substr(from, end - from);
            if (!std::regex_search(window, target))
                ++found[file];
        }
    }
    return found;
}

} // namespace

TEST(MenuPlacementRegistry, EveryShowMenuAsyncNamesWhereItOpensOrIsOnTheShrinkOnlyList) {
    synth::test::source_scan::expectCountsMatch("showMenuAsync calls with no placement", callsWithoutPlacement(),
                                                kOptionsBuiltElsewhere, "MenuPlacementRegistryTests.cpp");
}

// Regression test for FRO1595: the card's map hook dropped the dot's area, so Return opened the menu at the mouse.
TEST(MenuPlacementRegistry, NoShowMenuHookIgnoresTheAreaItIsGiven) {
    const std::regex unnamedArea(R"(showMenu\s*=\s*\[[^\]]*\]\s*\([^)]*Rectangle<int>\s*\))");
    EXPECT_TRUE(synth::test::source_scan::countCallSites(unnamedArea, "").empty())
        << "a showMenu hook must use its area (keyboard-opened: anchor to it) or take no area at all";
}
