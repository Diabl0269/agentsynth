#include "ModRingPump.h"
#include "AudioEngine/AudioEngine.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Layout/ZoomFrozenCachedImage.h"
#include <cmath>

namespace synth::ui {

namespace {
// A value step under 1/2000 of the range is under a pixel on the largest knob.
constexpr float kMovedStep = 1.0f / 2000.0f;
} // namespace

ModRingPump::ModRingPump(GraphEditor& editor, std::vector<ModulationDisplayInfo>& liveInfos)
    : editor_(editor)
    , live_(liveInfos) {
    setComponentID("modRingPump");
    setInterceptsMouseClicks(false, false);
    setWantsKeyboardFocus(false);
    setAccessible(false);
    setBounds(0, 0, 1, 1);
}

ModRingPump::~ModRingPump() = default;

double ModRingPump::nowMs() const { return clock_ ? clock_() : juce::Time::getMillisecondCounterHiRes(); }

// A routing that moved since the cache was filled wakes the frames. When the set of routings changed, any one that is
// off zero counts: its card meets it on the next frame.
void ModRingPump::routingsRebuilt(const std::vector<ModulationDisplayInfo>& rebuilt) {
    bool moved = false;
    bool sameShape = rebuilt.size() == live_.size();
    for (size_t i = 0; sameShape && i < rebuilt.size(); ++i)
        sameShape = rebuilt[i].attenuverterNodeID == live_[i].attenuverterNodeID &&
                    rebuilt[i].sourceNodeID == live_[i].sourceNodeID && rebuilt[i].destNodeID == live_[i].destNodeID &&
                    rebuilt[i].destChannelIndex == live_[i].destChannelIndex;
    for (size_t i = 0; i < rebuilt.size() && !moved; ++i)
        moved = sameShape ? std::abs(rebuilt[i].modSignalValue - live_[i].modSignalValue) >= kMovedStep
                          : std::abs(rebuilt[i].modSignalValue) > 0.001f;
    if (moved)
        wake();
}

void ModRingPump::wake() {
    quietFrames_ = 0;
    awake_ = true;
}

void ModRingPump::sleep() { awake_ = false; }

// On the editor's tick rather than each display refresh: the ring repaints then land in the same window frame as the
// tick's other repaints (the cables' flow, the meters). Display rate cost a whole window paint and commit per refresh,
// most of the playing Load test project's main thread.
void ModRingPump::tick() {
    if (awake_)
        frame();
}

// One frame: the live values, then the rings of every card on screen. A card off screen is not stepped (its own
// 15 Hz tick follows the routings when it comes back).
void ModRingPump::frame() {
    frameTimesMs_[(size_t)frameCount_ % frameTimesMs_.size()] = nowMs();
    ++frameCount_;
    before_.resize(live_.size());
    for (size_t i = 0; i < live_.size(); ++i)
        before_[i] = live_[i].modSignalValue;
    editor_.getAudioEngine().refreshModulationDisplayValues(live_);
    bool active = false;
    for (size_t i = 0; i < live_.size() && !active; ++i)
        active = std::abs(live_[i].modSignalValue - before_[i]) >= kMovedStep;

    const auto visible = editor_.getVisibleCanvasRect();
    for (auto* card : editor_.getModuleComponents())
        if (card != nullptr && card->getBounds().toFloat().intersects(visible))
            active = card->tickModulationRings() || active;

    quietFrames_ = active ? 0 : quietFrames_ + 1;
    if (quietFrames_ >= kQuietFrames && awake_)
        sleep();
}

int ModRingPump::framesPerSecond() const {
    const double now = nowMs();
    int count = 0;
    for (int i = 0; i < juce::jmin(frameCount_, (int)frameTimesMs_.size()); ++i)
        count += now - frameTimesMs_[(size_t)i] <= 1000.0 ? 1 : 0;
    return count;
}

void ModRingPump::addTestModeState(juce::DynamicObject& into) const {
    into.setProperty("ringFramesPerSecond", framesPerSecond());
    into.setProperty("ringFrameCount", frameCount_);
    into.setProperty("ringFramesRunning", awake_);
    // Rings actually drawn over the cards' pictures (every card's live-layer paints), so a scenario sees the frames
    // reach the screen, not only the pump stepping them.
    int livePaints = 0;
    for (auto* card : editor_.getModuleComponents())
        if (auto* picture =
                card != nullptr ? dynamic_cast<ZoomFrozenCachedImage*>(card->getCachedComponentImage()) : nullptr)
            livePaints += picture->getLivePaintCountForTest();
    into.setProperty("liveRingPaints", livePaints);
}

} // namespace synth::ui
