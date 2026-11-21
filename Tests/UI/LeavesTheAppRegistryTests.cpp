// LeavesTheAppRegistryTests.cpp -- every place in Source that sends the user to another program (a file opened in its
// default app, a folder shown in the Finder, a page opened in the browser) is listed below with why that is the right
// place for it. Content the app can show itself (a pack's list of sources, a help text) is shown in the app instead:
// Settings > Sound packs used to open a pack's SOURCES.md in a text editor. A new call site in none of these fails here
// by name, so it is a decision, not an accident.
#include "SourceScan.h"
#include <gtest/gtest.h>
#include <map>
#include <regex>
#include <set>
#include <string>
#include <vector>

using synth::test::source_scan::countCallSites;
using synth::test::source_scan::expectCountsMatch;

namespace {

struct Exit {
    const char* file;
    int calls;
    const char* why;
};

const std::vector<Exit> kAllowed = {
    {"Source/MainComponent/MainComponent.h", 1, "web page: the app's default URL opener (links to the website)"},
    {"Source/UI/Settings/AccountSettingsTab.h", 1, "web page: account and billing pages live on the website"},
    {"Source/UI/Settings/AccountSettingsTab.cpp", 1, "web page: account and billing pages live on the website"},
    {"Source/UI/Settings/PreferencesSettingsTab/PreferencesSettingsTab.h", 1, "web page: links to the website"},
    {"Source/UI/Settings/SoundPacks/SoundPacksSettingsTab.h", 1, "web page: a pack's licence page"},
    {"Source/UI/Assistant/SignInDialog.cpp", 2, "web page: sign-in is confirmed on the website"},
    {"Source/UI/Assistant/AIChatComponent/AIChatComponent.h", 1, "web page: links in the chat"},
    {"Source/UI/Settings/AppearanceSettingsTab.cpp", 1, "Finder: the button is named Open Themes Folder"},
    {"Source/UI/Library/SoundBrowser/SoundBrowserShapeActions.cpp", 1, "Finder: the menu item is Show in Finder"},
    {"Source/UI/Library/SoundBrowser/SoundBrowserFolders.cpp", 1, "Finder: the menu item is Show in Finder"},
};

} // namespace

TEST(LeavesTheApp, EveryExitToAnotherProgramIsListedWithItsReason) {
    const auto scanned =
        countCallSites(std::regex(R"((?:startAsProcess|revealToUser|launchInDefaultBrowser)\s*\()"), std::string());
    ASSERT_GT(scanned.size(), 5u) << "the scan found almost nothing: has the source scan moved?";

    std::map<std::string, int> registered;
    std::set<std::string> files;
    for (const auto& entry : kAllowed) {
        EXPECT_TRUE(files.insert(entry.file).second) << entry.file << " is listed twice";
        EXPECT_GT(std::string(entry.why).size(), 10u) << entry.file << ": say why it leaves the app";
        registered[entry.file] += entry.calls;
    }
    expectCountsMatch("leave-the-app", scanned, registered, "LeavesTheAppRegistryTests.cpp (kAllowed)");
}

// Regression test for FRO1800: Show sources opened the pack's SOURCES.md in another program.
TEST(LeavesTheApp, ShowSourcesNoLongerOpensAFile) {
    const auto scanned = countCallSites(std::regex(R"(startAsProcess\s*\()"), std::string());
    for (const auto& [file, n] : scanned)
        EXPECT_EQ(file.find("SoundPack"), std::string::npos) << file << " opens a file outside the app again";
}
