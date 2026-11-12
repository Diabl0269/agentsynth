// DisplaysShowModulatedValueTests.cpp
// The guard of the bug kind "a display shows the knob's value while modulation moves the sound". It finds every
// in-card display itself: each Source/UI/ModuleViews file that defines a paint, paintOverChildren or timerCallback (or
// the tick a timer calls).
// Every one must be registered below with what it shows; a new display fails until it is. A display registered as
// showing a live value must read the processor's modulated getter (getModulated..., getCurrent...) and no display
// outside the known list may read a stored parameter value in those bodies.
//
// The known list (kKnownBaseValueDisplays) is shrink-only: those displays still draw the stored value and are the
// open sweep. Fixing one moves it to LiveValue; nothing is ever added to it.
// Regression test for FRO1728: the Wavetable display read the Position knob in paint and timerCallback.

#include "../SourceScan.h"
#include <gtest/gtest.h>
#include <map>
#include <regex>
#include <set>
#include <string>

namespace {

namespace scan = synth::test::source_scan;

enum class Shows {
    LiveValue,  // a parameter as the processor plays it now (knob plus modulation)
    LiveSignal, // audio or a meter, read from the processor's buffers
    EditorData, // what the user edits (points, steps, curves, a route's range), not a modulated parameter
    KnownBase,  // still the stored value: the open sweep, see kKnownBaseValueDisplays
};

struct Entry {
    Shows shows;
    const char* why;
};

// Every in-card display under Source/UI/ModuleViews, by file.
const std::map<std::string, Entry>& registeredDisplays() {
    static const std::map<std::string, Entry> displays{
        {"Source/UI/ModuleViews/WavetableDisplayComponent.h", {Shows::LiveValue, "getModulatedScanPosition/Warp"}},
        {"Source/UI/ModuleViews/FrequencyResponseComponent.h", {Shows::LiveValue, "getCurrentCutoff and friends"}},
        {"Source/UI/ModuleViews/ScopeComponent.h", {Shows::LiveSignal, "the module's output buffer"}},
        {"Source/UI/ModuleViews/GainReductionMeterComponent.h", {Shows::LiveSignal, "the gain-reduction meter"}},
        {"Source/UI/ModuleViews/ThresholdControlComponent.h",
         {Shows::EditorData, "the threshold the user sets, beside the live level"}},
        {"Source/UI/ModuleViews/CurveEditor/CurveDrawGesture.cpp", {Shows::EditorData, "the stroke being drawn"}},
        {"Source/UI/ModuleViews/CurveEditor/CurveEditorDraw.cpp", {Shows::EditorData, "the curve's points"}},
        {"Source/UI/ModuleViews/CurveEditor/CurveEditorPaint.cpp", {Shows::EditorData, "the curve's points"}},
        {"Source/UI/ModuleViews/ModCurve/ModCurveBox.cpp", {Shows::EditorData, "a route's response curve"}},
        {"Source/UI/ModuleViews/ModCurve/ModCurveBox.h", {Shows::EditorData, "a route's response curve"}},
        {"Source/UI/ModuleViews/ModCurve/ModCurveRangeHandle.cpp", {Shows::EditorData, "a route's range"}},
        {"Source/UI/ModuleViews/ModCurve/ModCurveWindow.cpp", {Shows::EditorData, "a route's response curve"}},
        {"Source/UI/ModuleViews/MorphMap/MorphMapInternal.h", {Shows::EditorData, "a stop's focus ring"}},
        {"Source/UI/ModuleViews/ShapeEditor/NoteKeyStrip.cpp", {Shows::EditorData, "the per-note keys"}},
        {"Source/UI/ModuleViews/ShapeEditor/PerNoteBar.cpp", {Shows::EditorData, "the per-note switch"}},
        {"Source/UI/ModuleViews/ShapeEditor/ShapeListEditor.cpp", {Shows::EditorData, "the shape list"}},
        {"Source/UI/ModuleViews/ShapeEditor/ShapeListEditorInternal.h", {Shows::EditorData, "the shape list rows"}},
        {"Source/UI/ModuleViews/ShapeEditor/ShapePicker.cpp", {Shows::EditorData, "the shape library"}},
        {"Source/UI/ModuleViews/ShapeEditor/ShapeStackPanel.cpp", {Shows::EditorData, "the shape stack"}},
        {"Source/UI/ModuleViews/StepGrid/StepGridChordPicker.cpp", {Shows::EditorData, "the chord picker"}},
        {"Source/UI/ModuleViews/StepGrid/StepGridPageStrip.cpp", {Shows::EditorData, "the step pages"}},
        {"Source/UI/ModuleViews/StepGrid/StepGridPaint.cpp", {Shows::EditorData, "the steps"}},
        {"Source/UI/ModuleViews/StepGrid/StepGridPolyHint.cpp", {Shows::EditorData, "the poly hint"}},
        {"Source/UI/ModuleViews/StepGrid/StepGridRandomizePanel.cpp", {Shows::EditorData, "the randomize panel"}},
        {"Source/UI/ModuleViews/SampleWaveformComponent.h", {Shows::KnownBase, "Sampler start and loop markers"}},
        {"Source/UI/ModuleViews/EQCurvePaint.cpp", {Shows::KnownBase, "EQ curve from the stored bands"}},
        {"Source/UI/ModuleViews/EQCurveGlide.cpp", {Shows::KnownBase, "EQ curve from the stored bands"}},
        {"Source/UI/ModuleViews/MorphMap/MorphMapPaint.cpp", {Shows::KnownBase, "Morph map cursor at stored X/Y"}},
        {"Source/UI/ModuleViews/ShapeEditor/ShapeWindowMap.cpp", {Shows::KnownBase, "the same cursor in the window"}},
    };
    return displays;
}

// Card-level displays painted from the card's own paint (no display body of their own here), still on the stored
// value. Each must still exist; a fixed one is removed.
const std::map<std::string, const char*>& knownCardLevelBaseDisplays() {
    static const std::map<std::string, const char*> displays{
        {"Source/UI/Graph/ModuleComponent/ModuleComponentModRings.cpp",
         "the knob ring draws the routing's depth band around the stored value, not where the value is now"},
        {"Source/UI/Graph/ModuleComponent/ModuleComponentEnvelopeCard.cpp",
         "the ADSR graph draws the stored stage times; a cable on a stage is not shown"},
    };
    return displays;
}

// How many base-value displays there were when the guard was built. The list may only shrink.
constexpr size_t kKnownBaseValueDisplaysWhenBuilt = 7;

const std::regex& displayBodyStart() {
    static const std::regex re(
        R"(\b(paint|paintOverChildren|timerCallback|tick)\s*\([^)]*\)\s*(const\s*)?(override\s*)?(noexcept\s*)?\{)");
    return re;
}

// A stored parameter value read directly: a parameter's get()/getValue(), the host's raw value, or one of the
// processors' knob getters that has a modulated twin.
const std::regex& baseRead() {
    static const std::regex re(
        R"(->get\s*\(\s*\)|\bgetValue\s*\(\s*\)|\bgetRawParameterValue\s*\(|\bgetScanPosition\s*\(|\bgetWarpSignature\s*\(\s*\))");
    return re;
}

const std::regex& modulatedRead() {
    static const std::regex re(R"(\bget(Modulated|Current)\w*\s*\()");
    return re;
}

// The text of every display body in `text` (brace-matched from each body start).
std::string displayBodies(const std::string& text) {
    std::string bodies;
    for (auto it = std::sregex_iterator(text.begin(), text.end(), displayBodyStart()); it != std::sregex_iterator();
         ++it) {
        size_t i = (size_t)(it->position() + it->length());
        const size_t start = i;
        for (int depth = 1; depth > 0 && i < text.size(); ++i)
            depth += text[i] == '{' ? 1 : text[i] == '}' ? -1 : 0;
        bodies += text.substr(start, i - start) + "\n";
    }
    return bodies;
}

std::map<std::string, std::string> discoveredDisplays() {
    std::map<std::string, std::string> found;
    for (const auto& [file, text] : scan::sourceTexts())
        if (file.rfind("Source/UI/ModuleViews/", 0) == 0)
            if (auto bodies = displayBodies(text); !bodies.empty())
                found[file] = bodies;
    return found;
}

} // namespace

TEST(DisplaysShowModulatedValue, EveryModuleViewsDisplayIsRegisteredWithWhatItShows) {
    const auto found = discoveredDisplays();
    ASSERT_GT(found.size(), 10u) << "the scan found no displays: is Source/ where the test expects it?";
    for (const auto& [file, bodies] : found)
        EXPECT_TRUE(registeredDisplays().count(file))
            << "not registered: " << file << " paints or ticks; add it to registeredDisplays() with what it shows";
    for (const auto& [file, entry] : registeredDisplays())
        EXPECT_TRUE(found.count(file)) << "stale: " << file << " no longer paints or ticks; remove it";
}

TEST(DisplaysShowModulatedValue, NoDisplayReadsAStoredValueUnlessItIsAKnownOffender) {
    for (const auto& [file, bodies] : discoveredDisplays()) {
        const auto entry = registeredDisplays().find(file);
        if (entry == registeredDisplays().end() || entry->second.shows == Shows::KnownBase)
            continue;
        std::smatch m;
        EXPECT_FALSE(std::regex_search(bodies, m, baseRead()))
            << file << " reads a stored value (" << m.str() << ") in paint or timerCallback; show the processor's "
            << "modulated value (as getModulatedScanPosition or getCurrentCutoff do), or say why the stored one is "
            << "right by registering it";
    }
}

TEST(DisplaysShowModulatedValue, EveryLiveValueDisplayReadsTheModulatedGetter) {
    const auto found = discoveredDisplays();
    for (const auto& [file, entry] : registeredDisplays()) {
        if (entry.shows != Shows::LiveValue || !found.count(file))
            continue;
        EXPECT_TRUE(std::regex_search(found.at(file), modulatedRead()))
            << file << " is registered as showing the live value (" << entry.why
            << ") but no paint or timerCallback reads a getModulated.../getCurrent... getter";
    }
}

TEST(DisplaysShowModulatedValue, TheKnownBaseValueListOnlyShrinks) {
    size_t known = knownCardLevelBaseDisplays().size();
    for (const auto& [file, entry] : registeredDisplays())
        known += entry.shows == Shows::KnownBase ? 1 : 0;
    EXPECT_LE(known, kKnownBaseValueDisplaysWhenBuilt)
        << "a display was added to the known base-value list; make it show the modulated value instead";

    const auto texts = scan::sourceTexts();
    for (const auto& [file, why] : knownCardLevelBaseDisplays())
        EXPECT_TRUE(texts.count(file)) << "stale: " << file << " is gone; remove it from the known list";
}
