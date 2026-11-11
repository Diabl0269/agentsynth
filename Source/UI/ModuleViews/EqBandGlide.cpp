// EqBandGlide.cpp -- see EqBandGlide.h.
#include "EqBandGlide.h"

#include "UI/Layout/KeyGlide/KeyedTween.h"

#include <algorithm>
#include <cmath>

namespace synth::ui {

namespace {
constexpr float kVisible = 0.001f; // below this presence a band is not drawn
constexpr std::int64_t kMorphKey = 0;

bool shapeDiffers(const EqBandGlide::Bands& a, const EqBandGlide::Bands& b) {
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i].type != b[i].type || a[i].slopeDbPerOct != b[i].slopeDbPerOct)
            return true;
    return false;
}
} // namespace

EqBandGlide::~EqBandGlide() = default;

bool EqBandGlide::isAnimating() const noexcept { return tween_->isAnimating() || morph_->isAnimating(); }

EqBandGlide::EqBandGlide(juce::Component& owner)
    : tween_(std::make_unique<KeyedPointTween>(owner))
    , morph_(std::make_unique<KeyedPointTween>(owner)) {
    // The curve is summed from the drawn bands, so every frame recomputes it; the owner repaints itself.
    const auto dirty = [this](juce::Rectangle<int>) {
        if (onChange)
            onChange();
    };
    tween_->onDirty = dirty;
    morph_->onDirty = dirty;
    morph_->setDurationMs(kTypeMorphMs);
}

EqBandGlide::Drawn EqBandGlide::drawn(const Bands& resting, float outputGain) const {
    Drawn out;
    out.dots = resting;
    out.curve = resting;
    out.outputGainDb = outputGain + tween_->offsetFor(kOutputKey).x;
    out.typeMorph = juce::jlimit(0.0f, 1.0f, morph_->offsetFor(kMorphKey).x);
    for (int b = 0; b < ParametricEQModule::kMaxBands; ++b) {
        const auto i = (size_t)b;
        auto& dot = out.dots[i];
        const auto place = tween_->offsetFor(placeKey(b));
        const auto shape = tween_->offsetFor(shapeKey(b));
        if (place.x != 0.0f)
            dot.freqHz = std::exp2(std::log2(resting[i].freqHz) + place.x);
        dot.gainDb = resting[i].gainDb + place.y;
        dot.q = std::max(ParametricEQModule::kMinQ, resting[i].q + shape.x);
        const float presence = juce::jlimit(0.0f, 1.0f, (resting[i].enabled ? 1.0f : 0.0f) + shape.y);
        if (!resting[i].enabled && presence > kVisible && ghost_[i].active) {
            dot.freqHz = ghost_[i].freqHz;
            dot.gainDb = ghost_[i].gainDb;
            dot.q = ghost_[i].q;
        }
        dot.enabled = presence > kVisible;
        out.presence[i] = presence;
        out.alpha[i] = presence * std::min(tween_->alphaFor(placeKey(b)), tween_->alphaFor(shapeKey(b)));
        out.curve[i] = dot;
        out.curve[i].gainDb = dot.gainDb * presence;
    }
    return out;
}

// A band coming or going fades where it stands instead of travelling to or from its hidden settings; a band that stays
// on glides from where it is drawn to where it now rests, but only for an undo or redo.
void EqBandGlide::sync(const Bands& before, float outputGainBefore, const Bands& after, float outputGainAfter,
                       bool restored) {
    if (shapeDiffers(before, after))
        morph_->retarget(kMorphKey, {1.0f, 0.0f}, {0.0f, 0.0f});
    if (!tween_->isAnimating())
        presenceGlide_.fill(false);
    const auto now = drawn(before, outputGainBefore);
    for (int b = 0; b < ParametricEQModule::kMaxBands; ++b) {
        const auto i = (size_t)b;
        const auto& from = now.dots[i];
        const auto& to = after[i];
        const float fromPresence = now.presence[i];
        const bool appears = fromPresence <= kVisible && to.enabled;
        const bool vanishes = !to.enabled && fromPresence > kVisible;
        const bool toggled = before[i].enabled != to.enabled;

        if (!restored && !toggled && !appears && !vanishes) {
            tween_->erase(placeKey(b)); // a drag or a plain edit lands at once
            if (!presenceGlide_[i])
                tween_->erase(shapeKey(b));
            continue;
        }
        ghost_[i].active = false;
        if (appears || !to.enabled) {
            tween_->erase(placeKey(b));
            if (vanishes)
                ghost_[i] = {true, from.freqHz, from.gainDb, from.q};
        } else {
            tween_->retarget(placeKey(b), {std::log2(from.freqHz), from.gainDb}, {std::log2(to.freqHz), to.gainDb},
                             true);
        }
        const float fromQ = appears ? to.q : from.q;
        tween_->retarget(shapeKey(b), {fromQ, fromPresence}, {to.q, to.enabled ? 1.0f : 0.0f}, true);
        presenceGlide_[i] = !restored;
        if (vanishes && tween_->offsetFor(shapeKey(b)).y == 0.0f)
            ghost_[i].active = false; // nothing animates (Reduce Motion, Off): it is simply gone
    }
    if (restored)
        tween_->retarget(kOutputKey, {now.outputGainDb, 0.0f}, {outputGainAfter, 0.0f}, true);
    else
        tween_->erase(kOutputKey);
}

} // namespace synth::ui
