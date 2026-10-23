#include "KeyHold.h"

namespace synth::ui {

namespace {
// A hold is one run of the frame pump; no real hand keeps a key down this long (ten minutes).
constexpr double kHoldRunMs = 600000.0;
} // namespace

KeyHold::KeyHold(juce::Component& owner)
    : owner_(owner)
    , pump_(owner) {}

void KeyHold::begin(int keyCode, const KeyGlideConfig& config, double tapDistance) {
    if (glide_.isHeld())
        return;
    glide_ = KeyGlide(config);
    glide_.press(host.nowMs());
    keyCode_ = keyCode;
    tap_ = tapDistance;
    glided_ = 0.0;
    ending_ = false;
    ++generation_;
    pump_.run(kHoldRunMs, [this] { tick(); });
}

// When the key is no longer down the hold ends one turn later: the frame pump must not be stopped from inside its own
// frame.
void KeyHold::tick() {
    if (!glide_.isHeld() || ending_)
        return;
    if (!keyIsDown(keyCode_)) {
        ending_ = true;
        juce::MessageManager::callAsync(
            [safe = juce::Component::SafePointer<juce::Component>(&owner_), this, generation = generation_] {
                if (safe != nullptr && generation == generation_)
                    end();
            });
        return;
    }
    glided_ += glide_.advance(host.nowMs(), speedFactor ? speedFactor() : 1.0);
    if (onFrame)
        onFrame();
}

void KeyHold::end() {
    if (!glide_.isHeld())
        return;
    pump_.stop();
    glide_.release();
    ending_ = false;
    if (onEnd)
        onEnd();
}

void KeyHold::checkKeyUp() {
    if (glide_.isHeld() && !keyIsDown(keyCode_))
        end();
}

} // namespace synth::ui
