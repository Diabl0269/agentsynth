#pragma once

#include <juce_core/juce_core.h>

/** Scale used by ThresholdControlComponent to map a live level onto the meter.
 *
 *  Unipolar  — gate / envelope CV in [0, 1] (ADSR).
 *  Bipolar   — CV / audio in [-1, 1] (Sample & Hold, Comparator).
 *  Decibels  — audio level in dB (Compressor / Limiter, when they adopt the control).
 */
enum class ThresholdScale { Unipolar, Bipolar, Decibels };

/** Live threshold telemetry for ThresholdControlComponent.
 *
 *  Audio-thread writers, UI-thread readers: every getter must be safe to call off the
 *  audio thread (atomics on the module are the usual implementation). The control owns
 *  its own timer and repaints *itself* only, so a ModuleComponent that is
 *  setBufferedToImage(true) is not invalidated on every meter tick.
 */
class ThresholdMeterSource {
public:
    virtual ~ThresholdMeterSource() = default;

    /** Current input level, in the units of `getThresholdScale()`. */
    virtual float getMeterLevel() const = 0;

    /** Threshold actually in force (knob plus any Threshold CV). */
    virtual float getEffectiveThreshold() const = 0;

    /** True while the detector is armed / the signal is over the threshold. */
    virtual bool isOverThreshold() const = 0;

    /** Monotonic count of rising edges. The UI flashes when this changes. */
    virtual int getTriggerCount() const = 0;

    virtual ThresholdScale getThresholdScale() const = 0;

    /** Param id of the threshold float — `trigThreshold` or `gateThreshold`, never
     *  `threshold` (Compressor / Limiter already own that id with a dB meaning). */
    virtual juce::String getThresholdParamID() const = 0;

    /** Floor of the Decibels scale. Ignored for Unipolar / Bipolar. */
    virtual float getThresholdMinDecibels() const { return -60.0f; }

    /** Caption drawn on the idle meter when `getMeterLevel()` is effectively silent. */
    virtual juce::String getMeterIdleLabel() const { return "no input"; }
};
