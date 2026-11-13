// DragCancelTests.cpp -- Esc during any mouse drag on the timeline's clip lane or the piano roll abandons it: the
// document is exactly what it was before the drag, the preview is gone, the release that follows commits nothing, and
// no undo step was added. Every drag gesture of the two surfaces is a row of kGestures; the discovery test scans each
// surface's drag-mode enum, so a new mode that is not listed fails here by name. Each gesture is driven twice from a
// fresh bed: once with Esc (must restore) and once without (must change something, or the gesture proves nothing).
//
// Keys go the way juce::ComponentPeer::handleKeyPress delivers them: from the focused component (the surface the
// press landed on, which grabbed focus in mouseDown) up through its parents, each offering the key to its own
// keyPressed, the first to use it ending the walk.
#include "../PianoRoll/PianoRollTestHelpers.h"
#include "../SourceScan.h"
#include "AppUndoManager.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "Transport/TransportService.h"
#include "UI/Timeline/TimelinePanelComponent/TimelinePanelComponent.h"
#include <functional>
#include <gtest/gtest.h>
#include <set>
#include <string>
#include <vector>

using synth::ui::TimelineClipLaneArea;
using synth::ui::TimelinePanelComponent;

namespace {

constexpr int kAlt = juce::ModifierKeys::altModifier;
constexpr int kCtrlAlt = juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::altModifier;

struct StubHost : synth::ui::TrackHeaderHost {
    std::vector<BindingOption> getAvailableTrackInNodes(synth::TrackId) override { return {}; }
    juce::String getNodeDisplayName(const juce::String&) override { return {}; }
    void bindTrackTo(synth::TrackId, const juce::String&) override {}
    void createAndBindTrackInNode(synth::TrackId) override {}
    void selectNodeInGraph(const juce::String&) override {}
    void deleteTrack(synth::TrackId) override {}
    void performTrackEdit(const std::function<void()>& mutation) override {
        if (mutation)
            mutation();
    }
    void addMidiTrack() override {}
    void addAudioTrack() override {}
    void addInstrumentTrack(const juce::String&, bool) override {}
    std::vector<PluginLaneOption> getAvailablePluginLaneOptions() const override { return {}; }
    synth::LaneId addPluginAutomationLane(const PluginLaneOption&) override { return {}; }
};

// A timeline panel with two MIDI tracks: clip A (beats 2 to 6, three notes) on the first, clip B (beats 12 to 16) on
// the second. The piano roll opens on clip A.
struct Bed {
    synth::TimelineDoc doc;
    StubHost host;
    AppUndoManager undo;
    synth::TransportService transport;
    TimelinePanelComponent panel;
    synth::ClipId clipA, clipB;
    std::vector<synth::NoteId> notes;

    Bed() {
        panel.setSize(1400, 700);
        panel.setTrackHeaderHost(&host);
        panel.setTimelineDoc(&doc);
        panel.setUndoManager(&undo);
        panel.setTransport(&transport);
        auto& view = panel.getViewState();
        view.pixelsPerBeat = 40.0;
        view.firstVisibleBeat = 0.0;
        view.snap = synth::ui::TimelineViewState::Snap::Quarter;
        view.snapEnabled = true;

        const auto first = doc.addTrack(synth::TrackKind::Midi, "First");
        const auto second = doc.addTrack(synth::TrackKind::Midi, "Second");
        clipA = doc.addClip(first, 2.0, 4.0, "A");
        clipB = doc.addClip(second, 12.0, 4.0, "B");
        notes.push_back(doc.addNote(clipA, makeNote(0.0, 60, 1.0)));
        notes.push_back(doc.addNote(clipA, makeNote(1.0, 64, 1.0)));
        notes.push_back(doc.addNote(clipA, makeNote(2.0, 67, 1.0)));
    }

    TimelineClipLaneArea& lane() { return panel.getClipLaneArea(); }
    synth::ui::PianoRollComponent& roll() { return panel.getPianoRoll(); }

    void openRoll() {
        panel.openPianoRoll(clipA);
        roll().setVelocityLaneVisible(false, false);
        roll().setHorizontalView(40.0, 2.0);
    }
};

// Everything a drag could have changed that the gesture owns: the document, the undo history, and whichever
// view-state the gesture writes on commit (the clip selection, the Range tool's span, the roll's note selection).
struct Observed {
    std::string document;
    int editSerial = 0;
    bool canUndo = false;
    std::string selection;
    std::string range;
};

std::string selectionOf(const std::vector<synth::ClipId>& ids) {
    std::string out;
    for (auto id : ids)
        out += std::to_string(id.value) + ",";
    return out;
}

std::string selectionOf(const std::vector<synth::NoteId>& ids) {
    std::string out;
    for (auto id : ids)
        out += std::to_string(id.value) + ",";
    return out;
}

Observed observe(Bed& bed, bool inRoll) {
    Observed seen;
    seen.document = juce::JSON::toString(bed.doc.toVar()).toStdString();
    seen.editSerial = bed.undo.getEditSerial();
    seen.canUndo = bed.undo.canUndo();
    if (inRoll) {
        seen.selection = selectionOf(bed.roll().getSelectionForTest().getSelected());
    } else {
        seen.selection = selectionOf(bed.panel.getClipSelection().getSelected());
        if (const auto span = bed.lane().getRangeSpan())
            seen.range = std::to_string(span->startBeat) + "-" + std::to_string(span->endBeat);
    }
    return seen;
}

// What the press, the drag and the cancel key are, for one gesture.
struct Plan {
    juce::Point<float> down;
    juce::Point<float> drag;
    int mods = 0;
    std::function<void()> afterDown; // the state the press set up, checked mid-drag
};

struct Gesture {
    const char* surface; // "TimelineClipLaneArea" or "PianoRollComponent"
    const char* mode;    // the DragMode enumerator the gesture runs in
    const char* name;
    bool inRoll;
    synth::ui::EditTool tool;
    // Sets the bed up (selection, sizing mode) and says where to press and drag.
    std::function<Plan(Bed&)> plan;
    // The press selects what it grabs, so the grabbed clip or note is selected before the picture is taken: then the
    // press itself changes nothing visible and any difference is the drag's. The Range tool clears the selection on a
    // press by design, so it opts out.
    bool preselect = true;
    // False for a drag whose preview is not a picture (a velocity scrub only recolours the notes after release).
    bool paintsPreview = true;
};

juce::Point<float> rightEdgeOf(juce::Rectangle<int> rect) {
    return {(float)rect.getRight() - 3.0f, (float)rect.getCentreY()};
}
juce::Point<float> leftEdgeOf(juce::Rectangle<int> rect) {
    return {(float)rect.getX() + 3.0f, (float)rect.getCentreY()};
}
juce::Point<float> moved(juce::Point<float> p, float dx, float dy = 0.0f) { return {p.x + dx, p.y + dy}; }

// Every drag gesture of the clip lane and the piano roll. A new DragMode enumerator needs a row here (the discovery
// test below), and a new way into an existing mode (a modifier, a tool, a sizing mode) belongs here too.
const std::vector<Gesture>& kGestures() {
    static const std::vector<Gesture> gestures = {
        // ---- The clip lane ----
        {"TimelineClipLaneArea", "Move", "move a clip", false, synth::ui::EditTool::Select,
         [](Bed& bed) {
             const auto at = centreOf(bed.lane().getClipRect(bed.clipA));
             return Plan{at, moved(at, 120.0f), 0, nullptr};
         }},
        {"TimelineClipLaneArea", "Move", "move a clip to another track", false, synth::ui::EditTool::Select,
         [](Bed& bed) {
             const auto at = centreOf(bed.lane().getClipRect(bed.clipA));
             const auto target = centreOf(bed.lane().getClipRect(bed.clipB));
             return Plan{at, {at.x + 40.0f, target.y}, 0, nullptr};
         }},
        {"TimelineClipLaneArea", "Move", "Option-drag copies a clip", false, synth::ui::EditTool::Select,
         [](Bed& bed) {
             const auto at = centreOf(bed.lane().getClipRect(bed.clipA));
             return Plan{at, moved(at, 200.0f), kAlt, nullptr};
         }},
        {"TimelineClipLaneArea", "ResizeRight", "trim a clip's right edge", false, synth::ui::EditTool::Select,
         [](Bed& bed) {
             const auto edge = rightEdgeOf(bed.lane().getClipRect(bed.clipA));
             return Plan{edge, moved(edge, 80.0f), 0, nullptr};
         }},
        {"TimelineClipLaneArea", "ResizeRight", "stretch a clip's right edge (Sizing: Stretch)", false,
         synth::ui::EditTool::Select,
         [](Bed& bed) {
             bed.lane().setClipSizingMode(synth::ClipSizingMode::Stretch);
             const auto edge = rightEdgeOf(bed.lane().getClipRect(bed.clipA));
             return Plan{edge, moved(edge, 80.0f), 0, nullptr};
         }},
        {"TimelineClipLaneArea", "ResizeRight", "Option-drag stretches a clip's right edge", false,
         synth::ui::EditTool::Select,
         [](Bed& bed) {
             const auto edge = rightEdgeOf(bed.lane().getClipRect(bed.clipA));
             return Plan{edge, moved(edge, 80.0f), kAlt, nullptr};
         }},
        {"TimelineClipLaneArea", "ResizeLeft", "trim a clip's left edge", false, synth::ui::EditTool::Select,
         [](Bed& bed) {
             const auto edge = leftEdgeOf(bed.lane().getClipRect(bed.clipA));
             return Plan{edge, moved(edge, 40.0f), 0, nullptr};
         }},
        {"TimelineClipLaneArea", "Marquee", "sweep a marquee over clips", false, synth::ui::EditTool::Select,
         [](Bed& bed) {
             const auto start = moved(centreOf(bed.lane().getClipRect(bed.clipA)), -120.0f);
             return Plan{start, centreOf(bed.lane().getClipRect(bed.clipB)), 0, nullptr};
         }},
        {"TimelineClipLaneArea", "Range", "drag a time range", false, synth::ui::EditTool::Range,
         [](Bed& bed) {
             const auto at = centreOf(bed.lane().getClipRect(bed.clipA));
             return Plan{at, moved(at, 200.0f), 0, nullptr};
         },
         false},
        {"TimelineClipLaneArea", "Draw", "draw a new clip", false, synth::ui::EditTool::Draw,
         [](Bed& bed) {
             const auto at = moved(centreOf(bed.lane().getClipRect(bed.clipB)), 160.0f);
             return Plan{at, moved(at, 160.0f), 0, nullptr};
         }},
        // ---- The piano roll ----
        {"PianoRollComponent", "Move", "move a note", true, synth::ui::EditTool::Select,
         [](Bed& bed) {
             const auto at = centreOf(bed.roll().getNoteRect(bed.notes[1]));
             return Plan{at, moved(at, 80.0f, -20.0f), 0, nullptr};
         }},
        {"PianoRollComponent", "Move", "Option-drag copies a note", true, synth::ui::EditTool::Select,
         [](Bed& bed) {
             const auto at = centreOf(bed.roll().getNoteRect(bed.notes[1]));
             return Plan{at, moved(at, 160.0f), kAlt, nullptr};
         }},
        {"PianoRollComponent", "Resize", "resize a note", true, synth::ui::EditTool::Select,
         [](Bed& bed) {
             const auto edge = rightEdgeOf(bed.roll().getNoteRect(bed.notes[1]));
             return Plan{edge, moved(edge, 80.0f), 0, nullptr};
         }},
        {"PianoRollComponent", "VelocityScrub", "scrub note velocity", true, synth::ui::EditTool::Select,
         [](Bed& bed) {
             const auto at = centreOf(bed.roll().getNoteRect(bed.notes[1]));
             return Plan{at, moved(at, 0.0f, -30.0f), kCtrlAlt, nullptr};
         },
         true, false},
        {"PianoRollComponent", "DrawNew", "draw a note", true, synth::ui::EditTool::Draw,
         [](Bed& bed) {
             const auto at = pointAt(bed.roll(), 5.0, 62);
             return Plan{at, moved(at, 60.0f), 0, nullptr};
         }},
        {"PianoRollComponent", "Marquee", "sweep a marquee over notes", true, synth::ui::EditTool::Select,
         [](Bed& bed) {
             const auto start = pointAt(bed.roll(), 7.0, 72);
             return Plan{start, pointAt(bed.roll(), 3.5, 58), 0, nullptr};
         }},
    };
    return gestures;
}

// juce::ComponentPeer::handleKeyPress in miniature: the focused component first, then each parent.
bool dispatchKey(juce::Component& focused, const juce::KeyPress& key) {
    for (auto* target = &focused; target != nullptr; target = target->getParentComponent())
        if (target->keyPressed(key))
            return true;
    return false;
}

// What the surface paints right now: a drag preview or readout is on screen exactly when this differs from the
// picture taken just after the press.
juce::Image paintOf(juce::Component& surface) {
    juce::Image image(juce::Image::ARGB, surface.getWidth(), surface.getHeight(), true, juce::SoftwareImageType());
    juce::Graphics g(image);
    surface.paintEntireComponent(g, false);
    return image;
}

bool sameImage(const juce::Image& a, const juce::Image& b) {
    if (a.getWidth() != b.getWidth() || a.getHeight() != b.getHeight())
        return false;
    for (int y = 0; y < a.getHeight(); ++y)
        for (int x = 0; x < a.getWidth(); ++x)
            if (a.getPixelAt(x, y) != b.getPixelAt(x, y))
                return false;
    return true;
}

struct Outcome {
    Observed before;
    Observed after;
    bool previewWasShown = false;
    bool previewShownAfterEsc = false;
    bool escUsed = false;
    bool rollStillOpen = true;
};

Outcome run(const Gesture& gesture, bool pressEsc) {
    Bed bed;
    if (gesture.inRoll)
        bed.openRoll();
    juce::Component& surface =
        gesture.inRoll ? static_cast<juce::Component&>(bed.roll()) : static_cast<juce::Component&>(bed.lane());
    if (gesture.inRoll)
        bed.roll().setActiveTool(gesture.tool);
    else
        bed.lane().setActiveTool(gesture.tool);

    const auto plan = gesture.plan(bed);
    if (gesture.preselect) {
        if (gesture.inRoll)
            bed.roll().getSelectionForTest().setSelection({bed.notes[1]});
        else
            bed.panel.getClipSelection().setSelection({bed.clipA});
    }
    const auto pristine = paintOf(surface);
    surface.mouseDown(leftClick(surface, plan.down, plan.mods));

    Outcome out;
    out.before = observe(bed, gesture.inRoll); // after the press: the press is not the drag
    surface.mouseDrag(leftDrag(surface, plan.drag, plan.down, plan.mods));
    out.previewWasShown = !gesture.paintsPreview || !sameImage(pristine, paintOf(surface));

    if (pressEsc) {
        // Focus: the press grabbed it for the surface; with no native window in a test the focused component reads
        // null, so the walk starts at the surface itself.
        auto* focused = juce::Component::getCurrentlyFocusedComponent();
        out.escUsed = dispatchKey(focused != nullptr ? *focused : surface, juce::KeyPress(juce::KeyPress::escapeKey));
        out.previewShownAfterEsc = !sameImage(pristine, paintOf(surface));
        // The pointer is still down: more movement must not resume the gesture.
        surface.mouseDrag(leftDrag(surface, moved(plan.drag, 30.0f), plan.down, plan.mods));
    }
    surface.mouseUp(leftDrag(surface, plan.drag, plan.down, plan.mods));

    out.after = observe(bed, gesture.inRoll);
    out.rollStillOpen = bed.panel.isPianoRollOpen() == gesture.inRoll;
    return out;
}

bool sameObserved(const Observed& a, const Observed& b) {
    return a.document == b.document && a.editSerial == b.editSerial && a.canUndo == b.canUndo &&
           a.selection == b.selection && a.range == b.range;
}

// The drag-mode enumerators a header declares, read from the source text.
std::set<std::string> enumeratorsOf(const char* header, const char* enumName) {
    std::set<std::string> names;
    const auto texts = synth::test::source_scan::sourceTexts();
    const auto it = texts.find(header);
    if (it == texts.end())
        return names;
    const std::regex pattern(std::string("enum class ") + enumName + R"(\s*\{([^}]*)\})");
    std::smatch match;
    if (!std::regex_search(it->second, match, pattern))
        return names;
    const std::string body = match[1];
    const std::regex wordPattern(R"(\w+)");
    for (std::sregex_iterator word(body.begin(), body.end(), wordPattern), end; word != end; ++word)
        names.insert(word->str());
    names.erase("None");
    return names;
}

} // namespace

TEST(DragCancel, EscMidDragRestoresTheDocumentAndLeavesNoUndoStep) {
    for (const auto& gesture : kGestures()) {
        SCOPED_TRACE(std::string(gesture.surface) + " / " + gesture.mode + ": " + gesture.name);

        const auto control = run(gesture, false);
        EXPECT_TRUE(control.previewWasShown) << "the drag never started, so the test proves nothing";
        EXPECT_FALSE(sameObserved(control.before, control.after))
            << "without Esc the release changes nothing, so cancelling it proves nothing";

        const auto cancelled = run(gesture, true);
        EXPECT_TRUE(cancelled.previewWasShown);
        EXPECT_TRUE(cancelled.escUsed) << "Esc mid-drag must be consumed by the drag, not fall through";
        EXPECT_FALSE(cancelled.previewShownAfterEsc) << "the preview or readout is still drawn after Esc";
        EXPECT_EQ(cancelled.before.document, cancelled.after.document) << "the document changed";
        EXPECT_EQ(cancelled.before.editSerial, cancelled.after.editSerial) << "an undo step was added";
        EXPECT_EQ(cancelled.before.canUndo, cancelled.after.canUndo);
        EXPECT_EQ(cancelled.before.selection, cancelled.after.selection)
            << "Esc must cancel the drag and not also clear or change the selection";
        EXPECT_EQ(cancelled.before.range, cancelled.after.range);
        EXPECT_TRUE(cancelled.rollStillOpen) << "Esc closed the piano roll instead of cancelling the drag";
    }
}

// Without a drag Esc keeps its old meaning: it clears the clip selection.
TEST(DragCancel, EscWithNoDragStillClearsTheClipSelection) {
    Bed bed;
    bed.panel.getClipSelection().setSelection({bed.clipA});
    EXPECT_TRUE(dispatchKey(bed.lane(), juce::KeyPress(juce::KeyPress::escapeKey)));
    EXPECT_TRUE(bed.panel.getClipSelection().isEmpty());
}

// A new drag mode on either surface has to be listed in kGestures, which is what makes it a gesture Esc is held to.
TEST(DragCancel, EveryDragModeOfBothSurfacesIsListed) {
    const std::map<std::string, std::set<std::string>> scanned = {
        {"TimelineClipLaneArea",
         enumeratorsOf("Source/UI/Timeline/TimelineClipLaneArea/TimelineClipLaneArea.h", "DragMode")},
        {"PianoRollComponent",
         enumeratorsOf("Source/UI/PianoRoll/PianoRollComponent/PianoRollComponent.h", "DragMode")},
    };
    for (const auto& [surface, modes] : scanned)
        ASSERT_FALSE(modes.empty()) << surface << ": the scan found no drag modes; has the enum moved?";

    std::map<std::string, std::set<std::string>> listed;
    for (const auto& gesture : kGestures())
        listed[gesture.surface].insert(gesture.mode);

    for (const auto& [surface, modes] : scanned) {
        for (const auto& mode : modes)
            EXPECT_TRUE(listed[surface].count(mode) > 0)
                << "not listed: " << surface << " DragMode::" << mode << " - add its gesture to kGestures";
        for (const auto& mode : listed[surface])
            EXPECT_TRUE(modes.count(mode) > 0) << "stale: " << surface << " has no DragMode::" << mode;
    }
}
