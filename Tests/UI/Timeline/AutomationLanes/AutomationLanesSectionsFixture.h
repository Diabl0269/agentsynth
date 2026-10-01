#pragma once

// AutomationLanesSectionsFixture.h -- the modulator Scene (a real MainComponent, a Filter with an open cutoff
// lane) plus one LFO modulator, with the view set so bars are easy to hit, and the helpers the sections tests
// drive the band with. Header-only; not registered in Tests/CMakeLists.txt.

#include "AutomationLanesModulatorFixture.h"
#include "UI/Timeline/AutomationLanes/Modulators/ModulatorBand.h"

namespace sections_test {

using namespace modulator_test;
using namespace automation_lanes_test;
using synth::ui::SectionBlock;
using synth::ui::SectionBlocks;

constexpr double kBeatsPerBar = 4.0;
constexpr double kPixelsPerBeat = 8.0;

inline double bar(int n) { return (double)(n - 1) * kBeatsPerBar; } // the beat where bar `n` starts

// Counts doc notifications, to prove a gesture is one mutation.
struct NotificationCounter : synth::TimelineDoc::Listener {
    int count = 0;
    void timelineChanged(const synth::TimelineDoc&) override { ++count; }
};

struct SectionsScene : Scene {
    juce::String lfoUuid;

    explicit SectionsScene(synth::ui::EditTool tool = synth::ui::EditTool::Draw) {
        auto& view = panel().getViewState();
        view.pixelsPerBeat = kPixelsPerBeat;
        view.firstVisibleBeat = 0.0;
        view.snap = synth::ui::TimelineViewState::Snap::Bar;
        addLfoFromLaneMenu();
        const auto lfos = nodesOf<LFOModule>();
        if (lfos.size() == 1)
            lfoUuid = lfos.front()->properties["uuid"].toString();
        panel().setActiveTool(tool);
    }

    synth::ui::ModulatorBand* band(int index = 0) { return panel().modulatorBandForTest(lane, index); }
    const synth::AutomationLane* levelLane() { return synth::ui::sectionsLaneFor(doc(), lfoUuid); }
    SectionBlocks blocks() {
        const auto* level = levelLane();
        return level != nullptr ? synth::ui::sectionsFromPoints(level->points) : SectionBlocks{};
    }
    float x(double beat) { return (float)(beat * kPixelsPerBeat); }
    float midY() { return (float)band()->getHeight() * 0.5f; }

    // A real press, drag and release on the band, between two beats (band coordinates).
    void drag(double fromBeat, double toBeat, juce::ModifierKeys mods = leftButton()) {
        dragAcross(*band(), {x(fromBeat), midY()}, {x(toBeat), midY()}, 8, mods);
    }
    void click(double beat) {
        auto& b = *band();
        b.mouseDown(makeClickEvent(b, {x(beat), midY()}, leftButton()));
        b.mouseUp(makeClickEvent(b, {x(beat), midY()}, leftButton()));
    }
    void doubleClick(double beat) {
        auto& b = *band();
        b.mouseDoubleClick(makeClickEvent(b, {x(beat), midY()}, leftButton()));
    }
    bool key(const juce::KeyPress& press) { return band()->keyPressed(press); }

    // Draws [bar(first), bar(last + 1)) with the Draw tool, whatever tool is active.
    void drawBars(int first, int last) {
        panel().setActiveTool(synth::ui::EditTool::Draw);
        drag(bar(first), bar(last + 1));
    }
};

// The breakpoints of a lane as [beat, value, curve] triples, for exact comparison.
struct Pt {
    double beat;
    double value;
    int curve;
    bool operator==(const Pt& o) const { return beat == o.beat && value == o.value && curve == o.curve; }
};
inline std::vector<Pt> pointsOf(const synth::AutomationLane* lane) {
    std::vector<Pt> out;
    if (lane != nullptr)
        for (const auto& p : lane->points)
            out.push_back({p.beat, p.value, p.curve});
    return out;
}

inline constexpr int kHold = static_cast<int>(synth::BreakpointCurve::Hold);

// What a sections lane holds for the given (beat, value) pairs: every point a Hold.
inline std::vector<Pt> holdPoints(std::initializer_list<std::pair<double, double>> list) {
    std::vector<Pt> out;
    for (const auto& [beat, value] : list)
        out.push_back({beat, value, kHold});
    return out;
}

} // namespace sections_test
