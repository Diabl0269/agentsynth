#pragma once

// AutomationLanesAmountFixture.h -- the modulator Scene (a real MainComponent, a Filter with an open cutoff lane)
// plus one LFO modulator added from the lane menu, with the view set so beats are easy to hit, and the helpers
// the amount-lane tests drive the band with. A gesture is delivered to whichever component really takes the
// press at that point (the band, or the curve editor inside it), the way JUCE routes a real click.
// Header-only; not registered in Tests/CMakeLists.txt.

#include "AutomationLanesModulatorFixture.h"
#include "UI/Timeline/AutomationLanes/Modulators/ModulatorAmountLane.h"
#include "UI/Timeline/AutomationLanes/Modulators/ModulatorBand.h"

namespace amount_test {

using namespace modulator_test;
using namespace automation_lanes_test;

constexpr double kPixelsPerBeat = 8.0;
inline constexpr int kHold = static_cast<int>(synth::BreakpointCurve::Hold);

// Counts doc notifications.
struct NotificationCounter : synth::TimelineDoc::Listener {
    int count = 0;
    void timelineChanged(const synth::TimelineDoc&) override { ++count; }
};

struct AmountScene : Scene {
    juce::String lfoUuid;
    juce::String attenUuid;

    explicit AmountScene(synth::ui::EditTool tool = synth::ui::EditTool::Select) {
        auto& view = panel().getViewState();
        view.pixelsPerBeat = kPixelsPerBeat;
        view.firstVisibleBeat = 0.0;
        view.snap = synth::ui::TimelineViewState::Snap::Bar;
        addLfoFromLaneMenu();
        const auto lfos = nodesOf<LFOModule>();
        if (lfos.size() == 1)
            lfoUuid = lfos.front()->properties["uuid"].toString();
        if (auto* r = row())
            attenUuid = r->getInfo().attenuverterUuid;
        panel().setActiveTool(tool);
    }

    synth::ui::ModulatorBand* band(int index = 0) { return panel().modulatorBandForTest(lane, index); }
    const synth::AutomationLane* amountLane() { return synth::ui::amountLaneFor(doc(), attenUuid); }
    float x(double beat) { return (float)(beat * kPixelsPerBeat); }
    float yFor(double amount) { return (float)band()->getEditor()->valueToY(amount); }
    float amount() { return parameter(attenUuid, "amount"); }
    void tick(double beat = 0.0) {
        synth::TransportService::PositionSnapshot at;
        at.ppq = beat;
        panel().updateFromTransport(at, 0.0);
    }

    // The component a real press at `p` (band coordinates) lands on, and `p` in its own coordinates.
    std::pair<juce::Component*, juce::Point<float>> target(juce::Point<float> p) {
        auto& b = *band();
        auto* hit = b.getComponentAt(p.roundToInt());
        if (hit == nullptr)
            return {nullptr, p};
        return {hit, hit->getLocalPoint(&b, p)};
    }
    void drag(juce::Point<float> from, juce::Point<float> to, int steps = 8, juce::ModifierKeys mods = leftButton()) {
        auto [hit, local] = target(from);
        ASSERT_NE(hit, nullptr);
        dragAcross(*hit, local, local + (to - from), steps, mods);
    }
    void doubleClick(juce::Point<float> p) {
        auto [hit, local] = target(p);
        ASSERT_NE(hit, nullptr);
        hit->mouseDown(makeClickEvent(*hit, local, leftButton()));
        hit->mouseUp(makeClickEvent(*hit, local, leftButton()));
        hit->mouseDoubleClick(makeClickEvent(*hit, local, leftButton()));
    }
    // A pen stroke with the Draw tool from (fromBeat, fromAmount) to (toBeat, toAmount).
    void penStroke(double fromBeat, double fromAmount, double toBeat, double toAmount) {
        panel().setActiveTool(synth::ui::EditTool::Draw);
        drag({x(fromBeat), yFor(fromAmount)}, {x(toBeat), yFor(toAmount)}, 12);
    }
    void flush() { juce::MessageManager::getInstance()->runDispatchLoopUntil(20); }
};

} // namespace amount_test
