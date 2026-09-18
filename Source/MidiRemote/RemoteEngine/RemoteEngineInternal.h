#pragma once

// Private helpers shared by RemoteEngine's translation units (RemoteEngineDecode.cpp reads the
// classification/decode half, RemoteEngineApply.cpp the takeover-math half). Nothing here is part
// of the engine's public surface -- everything lives in synth::midi::detail and every function is
// `inline` so this header needs no matching .cpp of its own and can be included from more than one
// translation unit without violating ODR.

#include "MidiRemote/RemoteEngine/RemoteEngine.h" // kRelativeSensitivity
#include "MidiRemote/RemoteModel.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

namespace synth::midi::detail {

// -- Message classification (RemoteEngineDecode.cpp, handleMessage step 2) ----------------------

/** What handleMessage needs to know about a raw juce::MidiMessage before it can be looked up or
 *  decoded. `eligible` is false for anything that is not cc / noteOn / noteOff / pitchBend /
 *  channelPressure / programChange -- clock, active sensing, sysex and poly (per-note) aftertouch
 *  are never eligible and must never reach findSlot or the learn tally
 * (docs/control/midi-remote.md#are-mapped-messages-consumed-or-also-forwarded-to-the-graph,
 *  docs/control/midi-remote.md#learn-what-does-the-first-message-mean). */
struct ClassifiedMessage {
    bool eligible = false;
    synth::MessageType type = synth::MessageType::cc;
    int channel = 0;         // 1..16, exactly as received (never the profile's 0 = "any")
    int number = 0;          // cc/note number; 0 for pitchBend/channelPressure/programChange
    bool isNoteOn = false;   // true only for an actual note-on with velocity > 0
    bool isNoteOff = false;  // true for a real note-off OR a note-on with velocity 0
    int rawValue = 0;        // cc value / velocity (0..127) / channel-pressure value / program number
    int pitchWheelValue = 0; // 0..16383; pitchBend only
};

inline ClassifiedMessage classifyMessage(const juce::MidiMessage& message) noexcept {
    ClassifiedMessage result;

    if (message.isController()) {
        result.eligible = true;
        result.type = synth::MessageType::cc;
        result.channel = message.getChannel();
        result.number = message.getControllerNumber();
        result.rawValue = message.getControllerValue();
    } else if (message.isNoteOn(false) || message.isNoteOff(true)) {
        // isNoteOn(false): velocity > 0 only. isNoteOff(true): a real note-off OR a note-on with
        // velocity 0 -- together they classify every Note On/Off status byte exactly once.
        result.eligible = true;
        result.type = synth::MessageType::note;
        result.channel = message.getChannel();
        result.number = message.getNoteNumber();
        result.isNoteOn = message.isNoteOn(false);
        result.isNoteOff = !result.isNoteOn;
        result.rawValue = message.getVelocity();
    } else if (message.isPitchWheel()) {
        result.eligible = true;
        result.type = synth::MessageType::pitchBend;
        result.channel = message.getChannel();
        result.pitchWheelValue = message.getPitchWheelValue();
    } else if (message.isChannelPressure()) {
        result.eligible = true;
        result.type = synth::MessageType::channelPressure;
        result.channel = message.getChannel();
        result.rawValue = message.getChannelPressureValue();
    } else if (message.isProgramChange()) {
        result.eligible = true;
        result.type = synth::MessageType::programChange;
        result.channel = message.getChannel();
        result.number = message.getProgramChangeNumber();
        result.rawValue = message.getProgramChangeNumber();
    }
    // Everything else -- poly (per-note) aftertouch, clock, active sensing, sysex, and the
    // transport realtime bytes -- stays ineligible and is dropped by the caller before any ring
    // write, per docs/control/midi-remote.md#are-mapped-messages-consumed-or-also-forwarded-to-the-graph's step 2.

    return result;
}

/** The raw, normalised (0..1) value a learnCandidate event (or an activity-only decode) carries:
 *  velocity/127 for note-on, 0 for note-off, value/127 for cc/channelPressure, the 14-bit pitch
 *  wheel value/16383 for pitchBend. Meaningless for programChange (no learn ever needs it there). */
inline float rawNormalisedValue(const ClassifiedMessage& msg) noexcept {
    switch (msg.type) {
    case synth::MessageType::note:
        return msg.isNoteOn ? static_cast<float>(msg.rawValue) / 127.0f : 0.0f;
    case synth::MessageType::cc:
    case synth::MessageType::channelPressure:
        return static_cast<float>(msg.rawValue) / 127.0f;
    case synth::MessageType::pitchBend:
        return static_cast<float>(msg.pitchWheelValue) / 16383.0f;
    case synth::MessageType::programChange:
    default:
        return 0.0f;
    }
}

// -- Continuous-encoding decode (RemoteEngineDecode.cpp, handleMessage step 6) -------------------

inline float decodeAbs7(int value7Bit) noexcept { return static_cast<float>(value7Bit) / 127.0f; }

inline float decodeAbs7PitchBend(int pitchWheelValue14Bit) noexcept {
    return static_cast<float>(pitchWheelValue14Bit) / 16383.0f;
}

inline float decodeRelTwos(int ccValue) noexcept {
    const int signedValue = ccValue < 64 ? ccValue : ccValue - 128;
    return static_cast<float>(signedValue) * synth::midi::kRelativeSensitivity;
}

inline float decodeRelBinOffset(int ccValue) noexcept {
    return static_cast<float>(ccValue - 64) * synth::midi::kRelativeSensitivity;
}

inline float decodeRelSignMag(int ccValue) noexcept {
    const int magnitude = ccValue & 0x3f;
    const float signedMagnitude =
        (ccValue & 0x40) != 0 ? -static_cast<float>(magnitude) : static_cast<float>(magnitude);
    return signedMagnitude * synth::midi::kRelativeSensitivity;
}

// -- Takeover math (RemoteEngineApply.cpp, applyToParameter) -------------------------------------

/** Maps a raw 0..1 hardware value through an assignment's [rangeMin, rangeMax], clamped to 0..1.
 *  rangeMin > rangeMax is a legitimate inversion this formula already handles. */
inline float mapThroughRange(float raw, double rangeMin, double rangeMax) noexcept {
    const double hw = rangeMin + static_cast<double>(raw) * (rangeMax - rangeMin);
    return juce::jlimit(0.0f, 1.0f, static_cast<float>(hw));
}

/** Pick-up: true once the hardware value has crossed the parameter's current value since the
 *  previous hardware value seen for this gesture. */
inline bool pickupHasCrossed(float prevHw, float cur, float hw) noexcept {
    return (prevHw <= cur && hw >= cur) || (prevHw >= cur && hw <= cur);
}

/** Scale takeover's converging target: the parameter moves toward hw proportionally to how far hw
 *  itself moved since prevHw, reaching hw exactly when prevHw reaches either rail. Clamped to
 *  0..1. */
inline float scaleTarget(float cur, float prevHw, float hw) noexcept {
    float target = cur;
    if (hw > prevHw)
        target = cur + (hw - prevHw) * (prevHw < 1.0f ? (1.0f - cur) / (1.0f - prevHw) : 1.0f);
    else if (hw < prevHw)
        target = cur - (prevHw - hw) * (prevHw > 0.0f ? cur / prevHw : 1.0f);
    return juce::jlimit(0.0f, 1.0f, target);
}

} // namespace synth::midi::detail
