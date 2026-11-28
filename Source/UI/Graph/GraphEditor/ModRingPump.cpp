#include "ModRingPump.h"
#include "AudioEngine/AudioEngine.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
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
    if (frames_ == nullptr)
        frames_ = std::make_unique<juce::VBlankAttachment>(this, [this] { frame(); });
}

// The attachment is dropped from the message queue, never from inside its own callback.
void ModRingPump::sleep() {
    awake_ = false;
    juce::MessageManager::callAsync([safe = juce::Component::SafePointer<ModRingPump>(this)] {
        if (safe != nullptr && !safe->awake_)
            safe->frames_.reset();
    });
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
}

} // namespace synth::ui
