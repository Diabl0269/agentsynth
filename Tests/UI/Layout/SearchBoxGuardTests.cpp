// SearchBoxGuardTests.cpp -- every search or filter box in Source is the one SearchPillField (docs/layout/
// search-field.md). A raw juce::TextEditor (or NavigationSearchField) named for a search or filter, or given a
// "Search..." placeholder, ships square and without its hint and clear button; this fails by name so a new box has
// to be the shared one. No exceptions beyond the named, justified list below.

#include "../SourceScan.h"
#include <gtest/gtest.h>

namespace {

namespace scan = synth::test::source_scan;

// Files that may name NavigationSearchField as a type. Each is the base of the shared field itself, or not a search
// box at all.
const std::set<std::string>& navigationFieldUsers() {
    static const std::set<std::string> users = {
        "Source/UI/Layout/NavigationSearchField.h", // the base class
        "Source/UI/Layout/SearchPillField.h",       // the shared field built on it
        "Source/UI/Layout/SearchPillField.cpp",
        "Source/UI/Graph/ModDot/ModDotSourceRow.h", // an inline numeric amount editor, not a search box
        "Source/UI/Graph/ModDot/ModDotSourceRow.cpp",
    };
    return users;
}

TEST(SearchBoxGuardTest, EverySearchOrFilterFieldIsASearchPillField) {
    // A field declared as a plain TextEditor (a member or a unique_ptr) under a search or filter name.
    const std::regex rawField(
        R"((?:juce::TextEditor|NavigationSearchField)\s*>?\s+\w*(?:[sS]earch|[fF]ilter)\w*\s*(?:;|\{|=|,))");
    int checked = 0;
    for (const auto& [file, text] : scan::sourceTexts()) {
        ++checked;
        for (auto it = std::sregex_iterator(text.begin(), text.end(), rawField); it != std::sregex_iterator(); ++it)
            ADD_FAILURE() << file << " declares a raw search box (" << it->str()
                          << "): use synth::ui::SearchPillField (Source/UI/Layout/SearchPillField.h)";
    }
    EXPECT_GT(checked, 500) << "the scan must see the whole of Source";
}

TEST(SearchBoxGuardTest, NoOtherFileUsesTheBareNavigationField) {
    const std::regex use(R"(\bNavigationSearchField\b)");
    for (const auto& [file, text] : scan::sourceTexts())
        if (navigationFieldUsers().count(file) == 0 && std::regex_search(text, use))
            ADD_FAILURE() << file << " uses NavigationSearchField directly: a search box is a SearchPillField";
}

TEST(SearchBoxGuardTest, NoFieldGetsASearchPlaceholderOfItsOwn) {
    // setTextToShowWhenEmpty("Search ...") or ("Filter ...") on anything but the shared field's own setPlaceholder.
    const std::regex placeholder(R"(setTextToShowWhenEmpty\(\s*"(?:Search|Filter)[^"]*")");
    for (const auto& [file, text] : scan::sourceTexts())
        if (file != "Source/UI/Layout/SearchPillField.cpp" && std::regex_search(text, placeholder))
            ADD_FAILURE() << file << " gives a field a search placeholder itself: use SearchPillField::setPlaceholder";
}

} // namespace
