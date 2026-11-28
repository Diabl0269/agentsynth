#pragma once

#include "AudioEngine/ModulationRoutingTypes.h"
#include "TestMode/TestModeStateSource.h"
#include <array>
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <vector>

class GraphEditor;

namespace synth::ui {

/** Drives the modulation rings at the editor's 30 Hz tick while a modulation moves: one frame per tick, each reading
 * the live modulation values and stepping the rings of the cards on screen (ModuleComponent::tickModulationRings). It
 *  runs from the first moving routing until 30 quiet frames have passed, so an idle canvas costs nothing.
 *  A hidden 1x1 child of the editor that reports to test mode (id `modRingPump`). Message thread only. */
class ModRingPump
    : public juce::Component
    , public synth::testmode::StateSource {
public:
    /** `liveInfos` is the editor's routing display cache, which frames refresh in place. */
    ModRingPump(GraphEditor& editor, std::vector<ModulationDisplayInfo>& liveInfos);
    ~ModRingPump() override;

    /** The editor's 30 Hz tick, with the rebuilt display info before it replaces the cache: starts the frames when a
     *  routing's value moved since the cache was filled. */
    void routingsRebuilt(const std::vector<ModulationDisplayInfo>& rebuilt);
    /** The same tick, once the cache holds the rebuilt info: one ring frame while the frames run. */
    void tick();

    bool running() const noexcept { return awake_; }
    /** Frames run in the last second; 0 once the frames have stopped. */
    int framesPerSecond() const;

    /** One frame, as the tick runs it. */
    void runFrameForTest() { frame(); }
    /** A replacement wall clock (ms) for the frame rate; forgets the frames run so far. */
    void setClockForTest(std::function<double()> clock) {
        clock_ = std::move(clock);
        frameCount_ = 0;
    }

    void addTestModeState(juce::DynamicObject& into) const override;

    static constexpr int kQuietFrames = 30;

private:
    void frame();
    void wake();
    void sleep();
    double nowMs() const;

    GraphEditor& editor_;
    std::vector<ModulationDisplayInfo>& live_;
    std::vector<float> before_;
    bool awake_ = false;
    int quietFrames_ = 0;
    int frameCount_ = 0;
    std::array<double, 512> frameTimesMs_{};
    std::function<double()> clock_;
};

} // namespace synth::ui
