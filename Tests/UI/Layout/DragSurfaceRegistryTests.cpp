// DragSurfaceRegistryTests.cpp -- every class in Source that overrides mouseDrag is named below, with the guard that
// checks its preview against its commit (a drag shows a preview and the release writes the same thing), or the
// reason it has no preview/commit split. A new dragging class that is in neither list fails here by name, so a new
// drag surface cannot ship without choosing its guard. Gaps that are known and ticketed live in kKnownGaps; the list
// may only shrink (docs/layout/undo-glide.md, "Registration").
#include "../SourceScan.h"
#include "PointMarkers/PointMarkerSurfaceNames.h"
#include "TestPaths.h"
#include "UndoGlideSurfaces.h"
#include <algorithm>
#include <gtest/gtest.h>
#include <map>
#include <regex>
#include <set>
#include <string>
#include <vector>

using synth::test::source_scan::sourceTexts;

namespace {

// A guard is one of:
//   "clip-edge"                dragEdgeExpectingPreviewEqualsCommit (TimelineClipLaneTestFixture.h)
//   "point-markers:<name>"     an entry of pointSurfaces() (PointMarkers/)
//   "undo-glide:<name>"        an entry of undo_glide_test::surfaces()
//   "test:<Suite.Name>"        another registered test that checks preview == commit for the class
//   "exempt: <reason>"         a drag with no preview/commit split
struct Registered {
    const char* cls;
    std::vector<const char*> guards;
};

struct KnownGap {
    const char* cls;
    const char* ticket;
    const char* what;
};

const char* const kOnCardRows = "test:OnCardEditorRows.TheCommittedLayoutEqualsTheLastPreview";

const std::vector<Registered> kRegistered = {
    // Preview and commit checked by a shared guard.
    {"TimelineClipLaneArea", {"clip-edge"}},
    {"CurveEditorComponent", {"undo-glide:Curve editor point", "point-markers:Curve editor point"}},
    {"MorphMap", {"undo-glide:Morph map dot", "point-markers:Morph map dot"}},
    {"AutomationLaneEditor", {"undo-glide:Automation lane point", "point-markers:Automation lane point"}},
    // Preview and commit checked by a test of their own.
    {"CardLayoutOutline", {kOnCardRows}},
    {"CardLayoutAddRow", {kOnCardRows}},
    {"CardLayoutGroupHandle", {"test:OnCardGroupMoveEditor.WhatTheDragShowsIsWhatTheDropWrites"}},
    {"DrumMachineComponent",
     {"test:DrumMachineEdgeDragTest.DraggingPreviewsTheGhostsAndCommitsExactlyThePreviewAsOneUndoStep"}},
    {"DrumHitLane", {"test:DrumMachineLanesTest.ADragAcrossAnyLaneSetsARampAsOneUndoStepAndCommitsWhatItPreviewed"}},
    {"MixerInsertList",
     {"test:MixerInsertListReorderTests.ARealDragReordersAsOneUndoStepAndThePreviewIsWhatTheDropCommits"}},
    {"DragHandle", {"test:MacroPortConfigDialogTest.DraggingAcrossTwoRowsCommitsOnceOnReleaseWithTheFinalIndex"}},
    {"ControllerSurfaceCell", {"test:ControllerSurfaceComponentTest.MultiStepDragLandsExactlyOnOriginPlusDelta"}},
    // No preview/commit split.
    {"RangeSlider", {"exempt: a slider that writes its value live while it drags"}},
    {"CardFader", {"exempt: a fader that writes its value live while it drags"}},
    {"CardKnobSlider", {"exempt: a knob that writes its value live while it drags"}},
    {"MixerFaderSlider", {"exempt: a fader that writes its value live while it drags"}},
    {"ModCurveRangeHandle", {"exempt: the range is written live inside one open gesture, the release only closes it"}},
    {"ModDotAmountBar", {"exempt: the amount is written live inside one open gesture, the release only closes it"}},
    {"ModulatorBand", {"exempt: the amount knob writes live inside one open gesture"}},
    {"EQCurveComponent", {"exempt: a band is written live inside one open gesture"}},
    {"MeterColourStopsEditor", {"exempt: the stop is written live, the committed change repeats the same value"}},
    {"BpmDragLabel", {"exempt: the tempo is written live while the label drags"}},
    {"StepGridComponent", {"exempt: paints the cells live; the release only closes the undo step"}},
    {"EdgeResizeHandle", {"exempt: resizes the panel live, the release adds nothing"}},
    {"PanelResizeHandle", {"exempt: resizes the panel live, the release only stores the size already shown"}},
    {"GrabEdge", {"exempt: resizes the side pane live, the release only stores the width already shown"}},
    {"MixerSectionDivider", {"exempt: resizes the sections live, the release only stores the heights shown"}},
    {"MinimapComponent", {"exempt: scrolls the view, nothing to commit"}},
    {"CanvasEdgeScrollDriver",
     {"exempt: only listens to where the pointer is while another drag runs, it has none of its own"}},
    {"ControllerSurfaceComponent", {"exempt: a selection marquee, nothing to commit"}},
    {"TopBar", {"exempt: moves the help window, nothing to commit"}},
    {"ModuleLibraryComponent", {"exempt: starts a drag-and-drop, the drop target owns the preview"}},
    {"SoundBrowserComponent", {"exempt: starts a drag-and-drop, the drop target owns the preview"}},
    {"DrawPenButton", {"exempt: a drag only cancels the press-and-hold timer"}},
    {"MixerColourDot", {"exempt: forwards the drag to MixerColumnHeader (its gap is listed there)"}},
    {"SwatchButton", {"exempt: forwards the drag to TimelineTrackHeaderComponent (its gap is listed there)"}},
    {"ContextMenuForwardingLabel",
     {"exempt: forwards the drag to TimelineTrackHeaderComponent (its gap is listed there)"}},
};

// A real preview/commit split with no guard that compares the two. Each needs one; the list may only shrink.
const std::vector<KnownGap> kKnownGaps = {
    {"GraphEditor", "FRO1319", "canvas card move: shown card bounds vs the node position after release"},
    {"ModuleComponent", "FRO1319", "module card move: shown card bounds vs the node position after release"},
    {"MacroCardComponent", "FRO1319", "macro card move: shown card bounds vs the node position after release"},
    {"CardLayoutEditorGrip", "FRO1316", "list editor row reorder: preview rows vs the committed order"},
    {"DockTabButton", "FRO1316", "bottom dock tab reorder: lifted tab order vs the saved order"},
    {"NoteRangePicker", "FRO1318", "range edge/band drag: shown range vs the range committed on release"},
    {"MixerColumnHeader", "FRO1316", "mixer column reorder: preview slot vs the committed order"},
    {"MixerSendList", "FRO1316", "send row reorder: preview slot vs the committed order"},
    {"MixerZonesRow", "FRO1318", "zone row drag: preview slot vs the committed zone and order"},
    {"PianoRollComponent", "FRO1317",
     "note move/resize/copy: ghost notes vs the notes committed (only the draw tool is checked)"},
    {"PianoRollVelocityLane", "FRO1317", "velocity stick/ramp gesture: shown velocities vs the committed ones"},
    {"AutomationLaneHeaderComponent", "FRO1316", "lane header reorder: preview slot vs the committed order"},
    {"LaneControlChip", "FRO1316", "lane chip reorder: preview order vs the committed order"},
    {"ModulatorRow", "FRO1316", "modulator row reorder: preview slot vs the committed order"},
    {"TimelineRulerComponent", "FRO1318", "marker drag: snapped preview vs the marker position committed"},
    {"TimelineTrackHeaderComponent", "FRO1316", "track row reorder: lifted row slot vs the committed order"},
};

// ---------------------------------------------------------------------------------------------------------------------
// Scanning
// ---------------------------------------------------------------------------------------------------------------------

// The class or struct whose body contains `pos`, found by walking back out of the braces; "" when there is none.
std::string enclosingClass(const std::string& text, size_t pos) {
    static const std::regex head(R"(\b(?:class|struct)\s+(\w+))");
    int depth = 0;
    for (size_t i = pos; i-- > 0;) {
        if (text[i] == '}') {
            ++depth;
        } else if (text[i] == '{' && depth-- == 0) {
            const size_t from = text.find_last_of(";}{", i == 0 ? 0 : i - 1);
            const auto declaration = text.substr(from == std::string::npos ? 0 : from + 1, i - (from + 1));
            for (std::sregex_iterator it(declaration.begin(), declaration.end(), head), end; it != end; ++it) {
                const auto before = declaration.find_last_not_of(" \t\n", (size_t)it->position(0) - 1);
                const bool templateArg =
                    before != std::string::npos && (declaration[before] == '<' || declaration[before] == ',');
                const bool enumClass =
                    before != std::string::npos && before >= 3 && declaration.compare(before - 3, 4, "enum") == 0;
                if (!templateArg && !enumClass)
                    return (*it)[1];
            }
            depth = 0;
        }
    }
    return "";
}

// Every class that overrides mouseDrag(const juce::MouseEvent&), with the file that declares it.
std::map<std::string, std::string> classesOverridingMouseDrag() {
    static const std::regex pattern(R"(void\s+mouseDrag\s*\(\s*const\s+juce::MouseEvent\s*&[^)]*\)\s*override)");
    std::map<std::string, std::string> classes;
    for (const auto& [file, text] : sourceTexts()) {
        if (text.find("mouseDrag") == std::string::npos)
            continue;
        for (std::sregex_iterator it(text.begin(), text.end(), pattern), end; it != end; ++it) {
            const auto cls = enclosingClass(text, (size_t)it->position(0));
            classes[cls.empty() ? "?" + file : cls] = file;
        }
    }
    return classes;
}

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

bool fixtureHasEdgeGuard() {
    const auto fixture =
        juce::File(TESTS_ROOT_DIR).getChildFile("UI/Timeline/TimelineClipLane/TimelineClipLaneTestFixture.h");
    return fixture.loadFileAsString().contains("dragEdgeExpectingPreviewEqualsCommit");
}

// Empty when `guard` is real, else what is wrong with it.
std::string checkGuard(const std::string& guard) {
    const auto colon = guard.find(':');
    const auto kind = guard.substr(0, colon);
    const auto arg = colon == std::string::npos ? std::string() : guard.substr(colon + 1);
    if (kind == "clip-edge")
        return fixtureHasEdgeGuard() ? "" : "dragEdgeExpectingPreviewEqualsCommit is gone from the clip lane fixture";
    if (kind == "undo-glide") {
        for (const auto& entry : undo_glide_test::surfaces())
            if (arg == entry.name)
                return "";
        return "no undo glide surface is named '" + arg + "'";
    }
    if (kind == "point-markers") {
        const auto names = pointMarkerSurfaceNames();
        return std::find(names.begin(), names.end(), arg) != names.end() ? ""
                                                                         : "no point editor is named '" + arg + "'";
    }
    if (kind == "test")
        return testIsRegistered(arg) ? "" : "no test is named '" + arg + "'";
    if (kind == "exempt")
        return arg.size() > 12 ? "" : "an exemption needs a reason";
    return "unknown guard '" + guard + "'";
}

} // namespace

TEST(DragSurfaces, EveryMouseDragOverrideIsRegistered) {
    const auto found = classesOverridingMouseDrag();
    ASSERT_GT(found.size(), 40u) << "the scan found almost nothing: has the mouseDrag signature changed?";

    std::set<std::string> registered, gaps;
    for (const auto& entry : kRegistered) {
        EXPECT_TRUE(registered.insert(entry.cls).second) << entry.cls << " is registered twice";
        EXPECT_FALSE(entry.guards.empty()) << entry.cls << ": needs a guard or an exemption";
        for (const auto* guard : entry.guards) {
            const auto problem = checkGuard(guard);
            EXPECT_TRUE(problem.empty()) << entry.cls << ": " << problem;
        }
        if (!found.count(entry.cls))
            ADD_FAILURE() << "stale: " << entry.cls << " no longer overrides mouseDrag - remove it from kRegistered";
    }
    for (const auto& gap : kKnownGaps) {
        gaps.insert(gap.cls);
        EXPECT_EQ(registered.count(gap.cls), 0u)
            << "fixed - remove " << gap.cls << " from kKnownGaps (" << gap.ticket << ")";
        if (!found.count(gap.cls))
            ADD_FAILURE() << "stale: " << gap.cls << " no longer overrides mouseDrag - remove it from kKnownGaps";
    }
    for (const auto& [cls, file] : found)
        if (!registered.count(cls) && !gaps.count(cls))
            ADD_FAILURE() << "not registered: " << cls << " (" << file
                          << ") overrides mouseDrag - add it to kRegistered in DragSurfaceRegistryTests.cpp with "
                             "the guard that checks its preview against its commit, or an exemption with a reason";
}
