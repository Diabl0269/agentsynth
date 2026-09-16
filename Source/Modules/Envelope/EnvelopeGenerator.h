#pragma once

#include <algorithm>
#include <cmath>

namespace synth {

/** The stage a single `EnvelopeGenerator` instance is currently in. `Idle` means the
 *  generator is silent and inactive (no note down, fully released); every other stage
 *  means a note is sounding or releasing.
 */
enum class EnvelopeStage { Idle, Attack, Hold, Decay, Sustain, Release };

/** Plain parameter bag for `EnvelopeGenerator::getNextSample`. Times are in seconds and may
 *  be zero (an instant stage); curves are bipolar shape amounts in [-1, 1].
 */
struct EnvelopeParameters {
    float attack = 0.001f; // seconds, >= 0
    float hold = 0.0f;
    float decay = 1.0f;
    float sustain = 1.0f; // level, 0..1
    float release = 0.015f;
    float attackCurve = -0.3f; // -1..+1
    float decayCurve = 0.65f;
    float releaseCurve = 0.65f;
};

/** A progress-based AHDSR envelope generator.
 *
 *  Unlike a rate-based envelope (e.g. `juce::ADSR`, which derives a per-sample increment from
 *  `level / time` and can divide by a near-zero rate), every timed stage here owns a `p` in
 *  [0, 1] that advances by `1 / (time * sampleRate)` per sample. The output level is always
 *  `stageStart + (stageTarget - stageStart) * shape(p, curve)`, so:
 *
 *   - A stage's duration and endpoints are exact regardless of curve, and a 0-second stage is
 *     genuinely instant -- it costs no samples of its own (see the cascade in `getNextSample`).
 *   - Changing a stage's time mid-ramp only changes the slope of the ongoing interpolation
 *     (the increment added to `p` each sample); it can never teleport the level, because `p`
 *     and `stageStart`/`stageTarget` don't change just because the time parameter did.
 *   - `(stage, p)` is directly usable as a UI playhead position.
 *
 *  Retrigger: `noteOn()` always enters Attack from the *current* level (whatever stage the
 *  generator was in, including mid-release) over the *full* attack time -- never from 0, and
 *  never instantly. This is deliberate: it is what makes a gapless note sequence re-articulate
 *  audibly instead of net-ing out to no edge at all.
 *
 *  Header-only, no allocation, no locks, no JUCE dependency. Parameters are passed in fresh on
 *  every `getNextSample` call -- there is deliberately no `setParameters` and no cached
 *  rate/coefficient state that could go stale relative to a live parameter change.
 */
class EnvelopeGenerator {
public:
    void setSampleRate(double sampleRate) noexcept { sampleDt_ = sampleRate > 0.0 ? 1.0 / sampleRate : 0.0; }

    void reset() noexcept {
        stage_ = EnvelopeStage::Idle;
        progress_ = 0.0f;
        stageStart_ = 0.0f;
        currentLevel_ = 0.0f;
    }

    /** Enter Attack from the current level, over the full attack time. Safe to call from any
     *  stage, including mid-decay or mid-release -- that is the whole point (FRO110).
     */
    void noteOn() noexcept {
        stageStart_ = currentLevel_;
        stage_ = EnvelopeStage::Attack;
        progress_ = 0.0f;
    }

    /** Enter Release from the current level. Safe to call from any stage. */
    void noteOff() noexcept {
        stageStart_ = currentLevel_;
        stage_ = EnvelopeStage::Release;
        progress_ = 0.0f;
    }

    float getNextSample(const EnvelopeParameters& params) noexcept {
        for (;;) {
            switch (stage_) {
            case EnvelopeStage::Idle:
                currentLevel_ = 0.0f;
                return currentLevel_;

            case EnvelopeStage::Sustain:
                // A level, not a ramp -- read live every sample so automation retargets an
                // already-settled voice smoothly instead of stepping.
                currentLevel_ = params.sustain;
                return currentLevel_;

            case EnvelopeStage::Attack:
            case EnvelopeStage::Hold:
            case EnvelopeStage::Decay:
            case EnvelopeStage::Release: {
                const float time = stageTime(params);
                progress_ = time <= 0.0f ? 1.0f : std::min(1.0f, progress_ + static_cast<float>(sampleDt_ / time));

                const float target = stageTarget(params);
                const float shaped = shape(progress_, stageCurve(params));
                currentLevel_ = stageStart_ + (target - stageStart_) * shaped;

                if (progress_ >= 1.0f) {
                    stageStart_ = currentLevel_;
                    stage_ = nextStage(stage_);
                    progress_ = 0.0f;
                    // A 0-second stage costs no samples of its own: if the stage we just
                    // entered is itself instant (or is Sustain/Idle, which have no timing at
                    // all), cascade straight into it within this same call rather than
                    // returning a sample that "belongs" to a skipped stage.
                    if (stageTime(params) <= 0.0f)
                        continue;
                }
                return currentLevel_;
            }
            }
        }
    }

    EnvelopeStage getStage() const noexcept { return stage_; }
    float getProgress() const noexcept { return progress_; }
    float getLevel() const noexcept { return currentLevel_; }
    bool isActive() const noexcept { return stage_ != EnvelopeStage::Idle; }

    /** `shape(0, c) == 0` and `shape(1, c) == 1` exactly for every `c`. `c == 0` is linear;
     *  `c > 0` is fast-first/slow-tail (the natural decay/release shape); `c < 0` is
     *  slow-first/fast-finish.
     */
    static float shape(float p, float c) noexcept {
        if (c == 0.0f)
            return p;
        const float n = 1.0f + 4.0f * std::abs(c);
        if (c > 0.0f)
            return 1.0f - std::pow(1.0f - p, n);
        return std::pow(p, n);
    }

private:
    static EnvelopeStage nextStage(EnvelopeStage current) noexcept {
        switch (current) {
        case EnvelopeStage::Attack:
            return EnvelopeStage::Hold;
        case EnvelopeStage::Hold:
            return EnvelopeStage::Decay;
        case EnvelopeStage::Decay:
            return EnvelopeStage::Sustain;
        case EnvelopeStage::Release:
            return EnvelopeStage::Idle;
        default:
            return current;
        }
    }

    float stageTime(const EnvelopeParameters& params) const noexcept {
        switch (stage_) {
        case EnvelopeStage::Attack:
            return params.attack;
        case EnvelopeStage::Hold:
            return params.hold;
        case EnvelopeStage::Decay:
            return params.decay;
        case EnvelopeStage::Release:
            return params.release;
        default:
            return 0.0f;
        }
    }

    float stageTarget(const EnvelopeParameters& params) const noexcept {
        switch (stage_) {
        case EnvelopeStage::Attack:
            return 1.0f;
        case EnvelopeStage::Hold:
            return 1.0f; // pinned: start (always 1 coming out of Attack) == target
        case EnvelopeStage::Decay:
            return params.sustain; // live, read per sample
        case EnvelopeStage::Release:
            return 0.0f;
        default:
            return 0.0f;
        }
    }

    float stageCurve(const EnvelopeParameters& params) const noexcept {
        switch (stage_) {
        case EnvelopeStage::Attack:
            return params.attackCurve;
        case EnvelopeStage::Decay:
            return params.decayCurve;
        case EnvelopeStage::Release:
            return params.releaseCurve;
        default:
            return 0.0f; // Hold is pinned; the curve can't do anything to a flat segment.
        }
    }

    EnvelopeStage stage_ = EnvelopeStage::Idle;
    float progress_ = 0.0f;
    float stageStart_ = 0.0f;
    float currentLevel_ = 0.0f;
    double sampleDt_ = 1.0 / 44100.0;
};

} // namespace synth
