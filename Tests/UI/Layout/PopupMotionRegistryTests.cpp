// PopupMotionRegistryTests.cpp -- every place in Source that opens or animates a popup through PopupMotion (attach,
// dismiss, dismissModal, dismissCallOut) is named below with the test that checks its open and close motion, an
// exemption with a reason, or a known gap. A new popup user in none of them fails here by name, so a popup cannot
// ship without its motion being checked. launchDialog call sites are registered by the theme sweep instead
// (ThemeSweepSurfaces.cpp). Known gaps may only shrink (docs/layout/animation.md, "Popups").
#include "../SourceScan.h"
#include <gtest/gtest.h>
#include <map>
#include <regex>
#include <set>
#include <string>
#include <vector>

using synth::test::source_scan::countCallSites;
using synth::test::source_scan::expectCountsMatch;

namespace {

// `guards` are "test:<Suite.Name>" (a registered test that checks the popup's open and close motion) or
// "exempt: <reason>". `calls` is the number of PopupMotion call sites in the file.
struct Opener {
    const char* file;
    int calls;
    std::vector<const char*> guards;
};

struct KnownGap {
    const char* file;
    int calls;
    const char* ticket;
};

const std::vector<Opener> kRegistered = {
    {"Source/UI/Layout/DetachablePanelHost/DetachedPanelWindow.cpp",
     2,
     {"test:DetachedPanelWindowMotion.TheWindowFadesInAndOutLikeAnyPopupAndNeverSlides"}},
    {"Source/UI/Graph/ModDot/ModDotPanelFrame.cpp",
     2,
     {"test:ModDotPanelFrameTest.ClosingShrinksThePanelFrameByFrameBackIntoItsDot",
      "test:PopupDismissPaths.TheModDotPanelShrinksIntoItsDotOnEscape",
      "test:PopupDismissPaths.TheModDotPanelShrinksIntoItsDotOnAClickOutside",
      "test:PopupDismissPaths.TheModDotPanelShrinksIntoItsDotWhenTheAppIsDeactivated"}},
    {"Source/UI/Library/ModuleLibraryComponent/ModuleLibraryHelpPopover.cpp",
     1,
     {"test:HelpPopoverClose.ThePinnedPanelFadesOutBeforeItLeavesAndIsStillPinnedWhileItDoes"}},
    {"Source/UI/Layout/DialogKeyboard.h",
     2,
     {"test:PopupMotionDismiss.CallOutDismissGoesThroughTheFadeToo",
      "test:PopupMotionDismiss.StartsAFadeAndClosesOnlyWhenItEnds"}},
    {"Source/UI/Theme/AppLookAndFeel/AppLookAndFeelWindowMotion.cpp",
     3,
     {"test:PopupMotionLookAndFeel.PopupMenuWindowHookAttachesAndKeepsAPeerlessMenuStill",
      "test:PopupMotionLookAndFeel.CallOutBoxIsAttachedAsItAsksForItsBorder"}},
    {"Source/UI/Layout/NoteLengthMenu/NoteLengthPopup.cpp",
     2,
     {"test:PopupDismissPaths.TheNoteLengthPopupFadesOnEscape",
      "test:PopupDismissPaths.TheNoteLengthPopupFadesOnAClickOutside",
      "test:PopupDismissPaths.TheNoteLengthPopupFadesWhenTheAppIsDeactivated"}},
    {"Source/UI/Graph/ModMatrixPicker.cpp",
     1,
     {"test:PopupDismissPaths.ACallOutFadesOnEscapeThroughItsLiveWindow",
      "test:PopupDismissPaths.ACallOutFadesOnAClickOutsideAndWhenTheAppIsDeactivated"}},
    {"Source/UI/Layout/KeyPicker/KeyPicker.cpp",
     1,
     {"test:PopupDismissPaths.ACallOutFadesOnEscapeThroughItsLiveWindow",
      "test:PopupDismissPaths.ACallOutFadesOnAClickOutsideAndWhenTheAppIsDeactivated"}},
    {"Source/UI/MidiRemote/MidiRemotePanel/MidiRemotePanelAssign.cpp",
     3,
     {"test:PopupDismissPaths.ACallOutFadesOnEscapeThroughItsLiveWindow",
      "test:PopupDismissPaths.ACallOutFadesOnAClickOutsideAndWhenTheAppIsDeactivated"}},
    {"Source/UI/MidiRemote/MidiRemotePanel/MidiRemotePanelControllers.cpp",
     2,
     {"test:PopupDismissPaths.ACallOutFadesOnEscapeThroughItsLiveWindow",
      "test:PopupDismissPaths.ACallOutFadesOnAClickOutsideAndWhenTheAppIsDeactivated"}},
    {"Source/UI/Timeline/AutomationLanes/LaneShapes/DrawShapeFlyout.cpp",
     1,
     {"test:PopupDismissPaths.ACallOutFadesOnEscapeThroughItsLiveWindow",
      "test:PopupDismissPaths.ACallOutFadesOnAClickOutsideAndWhenTheAppIsDeactivated"}},
    {"Source/UI/Timeline/TimelinePanelComponent/TimelinePanelShapes.cpp",
     1,
     {"test:PopupDismissPaths.ACallOutFadesOnEscapeThroughItsLiveWindow",
      "test:PopupDismissPaths.ACallOutFadesOnAClickOutsideAndWhenTheAppIsDeactivated"}},
    {"Source/UI/Graph/GraphEditor/GraphEditorMacroPrompts.cpp",
     4,
     {"test:PopupDismissPaths.AnAlertPromptFadesOnEscapeAndStaysOpenOnAClickOutsideOrDeactivation"}},
    {"Source/UI/Graph/CardLayoutEditor/PresetNamePrompt.cpp",
     1,
     {"test:PopupDismissPaths.AnAlertPromptFadesOnEscapeAndStaysOpenOnAClickOutsideOrDeactivation"}},
    {"Source/UI/MidiRemote/ControllersList/ControllersListComponent.cpp",
     1,
     {"test:PopupDismissPaths.AnAlertPromptFadesOnEscapeAndStaysOpenOnAClickOutsideOrDeactivation"}},
    {"Source/UI/Chrome/ConfirmDontAskDialog.cpp",
     1,
     {"test:PopupDismissPaths.AnAlertPromptFadesOnEscapeAndStaysOpenOnAClickOutsideOrDeactivation"}},
    {"Source/UI/ModuleViews/ShapeEditor/ShapeSavePrompts.cpp",
     1,
     {"test:PopupDismissPaths.AnAlertPromptFadesOnEscapeAndStaysOpenOnAClickOutsideOrDeactivation"}},
    {"Source/MainComponent/MainComponentTrackPresets.cpp",
     2,
     {"test:PopupDismissPaths.AnAlertPromptFadesOnEscapeAndStaysOpenOnAClickOutsideOrDeactivation"}},
    {"Source/MainComponent/MainComponentPanels.cpp",
     2,
     {"test:PopupDismissPaths.AnAlertPromptFadesOnEscapeAndStaysOpenOnAClickOutsideOrDeactivation"}},
    {"Source/MainComponent/MainComponentCallbacks.cpp",
     2,
     {"test:PopupDismissPaths.ADialogFadesOnEscapeAndStaysOpenOnAClickOutsideOrDeactivation"}},
    {"Source/MainComponent/MainComponentRenderToAudio.cpp",
     1,
     {"test:PopupDismissPaths.ADialogFadesOnEscapeAndStaysOpenOnAClickOutsideOrDeactivation"}},
};

// Opens and closes with PopupMotion, but no test watches the close frames. Known gaps may only shrink; none is left.
const std::vector<KnownGap> kKnownGaps = {};

bool testIsRegistered(const std::string& fullName) {
    const auto* unit = ::testing::UnitTest::GetInstance();
    for (int s = 0; s < unit->total_test_suite_count(); ++s) {
        const auto* suite = unit->GetTestSuite(s);
        for (int t = 0; t < suite->total_test_count(); ++t)
            if (std::string(suite->name()) + "." + suite->GetTestInfo(t)->name() == fullName)
                return true;
    }
    return false;
}

} // namespace

TEST(PopupMotionUsers, EveryUserIsRegistered) {
    const auto scanned =
        countCallSites(std::regex(R"(PopupMotion::(?:attach|dismiss|dismissModal|dismissCallOut)\s*\()"),
                       "Source/UI/Layout/PopupMotion.");
    ASSERT_GT(scanned.size(), 15u) << "the scan found almost nothing: have the PopupMotion entry points been renamed?";

    std::map<std::string, int> registered;
    std::set<std::string> files;
    for (const auto& entry : kRegistered) {
        EXPECT_TRUE(files.insert(entry.file).second) << entry.file << " is registered twice";
        registered[entry.file] += entry.calls;
        EXPECT_FALSE(entry.guards.empty()) << entry.file << ": needs a test or an exemption";
        for (const std::string guard : entry.guards) {
            if (guard.rfind("test:", 0) == 0)
                EXPECT_TRUE(testIsRegistered(guard.substr(5))) << entry.file << ": no test is named " << guard;
            else
                EXPECT_TRUE(guard.rfind("exempt: ", 0) == 0 && guard.size() > 12)
                    << entry.file << ": '" << guard << "' is neither test:<Suite.Name> nor exempt: <reason>";
        }
    }
    for (const auto& gap : kKnownGaps) {
        EXPECT_TRUE(files.insert(gap.file).second)
            << gap.file << " is in kKnownGaps and kRegistered: if it is fixed, remove it from kKnownGaps ("
            << gap.ticket << ")";
        registered[gap.file] += gap.calls;
    }
    expectCountsMatch("PopupMotion", scanned, registered, "PopupMotionRegistryTests.cpp (kRegistered)");
}
