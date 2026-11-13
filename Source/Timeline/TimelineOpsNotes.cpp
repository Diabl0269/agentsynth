// Concern: reading one note of a timelineOps placeClips clip.
#include "TimelineOpsNotes.h"

#include "TimelineValidator.h"

#include <cmath>
#include <initializer_list>
#include <limits>

namespace synth {

namespace {

bool readInt(const juce::var& v, int& out) {
    if (v.isInt()) {
        out = static_cast<int>(v);
        return true;
    }
    if (v.isInt64()) {
        // Route through juce::int64: on LP64 Linux std::int64_t is `long`, which juce::var can
        // convert to via BOTH its int and int64 operators - a direct cast is ambiguous there.
        const auto wide = static_cast<std::int64_t>(static_cast<juce::int64>(v));
        if (wide < std::numeric_limits<int>::min() || wide > std::numeric_limits<int>::max())
            return false;
        out = static_cast<int>(wide);
        return true;
    }
    return false;
}

// Accepts ints too: a JSON writer is free to emit 4 rather than 4.0 for a whole-numbered beat.
bool readDouble(const juce::var& v, double& out) {
    if (v.isDouble() || v.isInt() || v.isInt64()) {
        out = static_cast<double>(v);
        return true;
    }
    return false;
}

bool readOptionalInt(const juce::var& v, int& out) { return v.isVoid() || readInt(v, out); }
bool readOptionalDouble(const juce::var& v, double& out) { return v.isVoid() || readDouble(v, out); }

bool isBeatInBounds(double v) { return std::isfinite(v) && v >= 0.0 && v <= kMaxPpqUntrusted; }
bool isLengthInBounds(double v) { return std::isfinite(v) && v > 0.0 && v <= kMaxPpqUntrusted; }
juce::String beatRangeText() { return "0 and " + juce::String(kMaxPpqUntrusted); }

// A note is a CLOSED object: an unrecognised key is refused, not ignored (see TimelineOps.cpp's unknownKey).
juce::String firstUnknownKey(juce::DynamicObject& o, std::initializer_list<const char*> allowed) {
    for (int i = 0; i < o.getProperties().size(); ++i) {
        const juce::String key = o.getProperties().getName(i).toString();
        bool known = false;
        for (const char* candidate : allowed)
            known = known || key == candidate;
        if (!known)
            return key;
    }
    return {};
}

// "h/o" as plain digits on both sides, like the Poly Sequencer's step condition.
bool parseCondition(const juce::String& text, int& hit, int& of) {
    const int slash = text.indexOfChar('/');
    if (slash <= 0 || slash >= text.length() - 1)
        return false;
    const auto left = text.substring(0, slash);
    const auto right = text.substring(slash + 1);
    if (!left.containsOnly("0123456789") || !right.containsOnly("0123456789") || left.length() > 3 ||
        right.length() > 3)
        return false;
    hit = left.getIntValue();
    of = right.getIntValue();
    return true;
}

constexpr const char* kAcceptedKeys =
    "\"startBeat\", \"lengthBeats\", \"pitch\", \"velocity\", \"channel\", \"chance\", \"repeat\", \"flam\", "
    "\"nudge\" and \"condition\"";

juce::String readTiming(juce::DynamicObject& noteObj, MidiNote& out) {
    if (!readOptionalDouble(noteObj.getProperty("startBeat"), out.startBeat) ||
        !readOptionalDouble(noteObj.getProperty("lengthBeats"), out.lengthBeats))
        return "has a note with a non-numeric \"startBeat\" or \"lengthBeats\".";
    if (!isBeatInBounds(out.startBeat))
        return "has a note starting at beat " + juce::String(out.startBeat) + ", which is not a finite beat between " +
               beatRangeText() + ". Note beats are relative to the clip's own start.";
    if (!isLengthInBounds(out.lengthBeats))
        return "has a note " + juce::String(out.lengthBeats) +
               " beats long. A note length must be finite, greater than 0 and at most " +
               juce::String(kMaxPpqUntrusted) + ".";
    return {};
}

// Ranges are REJECTED, never clamped - validateTimeline's rule, for its reason: a hand on a
// control means "as far as this goes", but a sender naming pitch 200 means something the
// parameter cannot express, and quietly rewriting it to 127 ships music nobody asked for.
juce::String readPitchVelocityChannel(juce::DynamicObject& noteObj, MidiNote& out) {
    if (!readOptionalInt(noteObj.getProperty("pitch"), out.pitch) ||
        !readOptionalInt(noteObj.getProperty("velocity"), out.velocity) ||
        !readOptionalInt(noteObj.getProperty("channel"), out.channel))
        return "has a note with a non-integer \"pitch\", \"velocity\" or \"channel\".";
    if (out.pitch < 0 || out.pitch > 127)
        return "has a note with pitch " + juce::String(out.pitch) + ", outside the MIDI range 0 to 127.";
    if (out.velocity < 1 || out.velocity > 127)
        return "has a note with velocity " + juce::String(out.velocity) +
               ", outside the range 1 to 127 (0 is not a note-on).";
    if (out.channel < 1 || out.channel > 16)
        return "has a note on channel " + juce::String(out.channel) + ", outside the MIDI range 1 to 16.";
    return {};
}

juce::String readHitSettings(juce::DynamicObject& noteObj, MidiNote& out) {
    if (!readOptionalInt(noteObj.getProperty("chance"), out.chance) ||
        !readOptionalInt(noteObj.getProperty("repeat"), out.repeat))
        return "has a note with a non-integer \"chance\" or \"repeat\".";
    if (!MidiNote::isValidChance(out.chance))
        return "has a note with chance " + juce::String(out.chance) + ", outside the range 0 to 100 (a percent).";
    if (!MidiNote::isValidRepeat(out.repeat))
        return "has a note with repeat " + juce::String(out.repeat) + ", outside the range 1 to 8.";

    const auto flam = noteObj.getProperty("flam");
    if (!flam.isVoid()) {
        if (!flam.isBool())
            return "has a note whose \"flam\" is not true or false.";
        out.flam = static_cast<bool>(flam);
    }

    if (!readOptionalDouble(noteObj.getProperty("nudge"), out.nudge))
        return "has a note with a non-numeric \"nudge\".";
    if (!std::isfinite(out.nudge) || !MidiNote::isValidNudge(out.nudge))
        return "has a note with nudge " + juce::String(out.nudge) +
               ", outside the range -0.5 to 0.5 (a fraction of a grid step).";

    const auto condition = noteObj.getProperty("condition");
    if (!condition.isVoid()) {
        int hit = 1;
        int of = 1;
        if (!condition.isString() || !parseCondition(condition.toString(), hit, of))
            return "has a note whose \"condition\" is not text like \"2/4\".";
        if (!MidiNote::isValidCondition(hit, of))
            return "has a note with condition \"" + condition.toString() +
                   "\", which needs 1 <= hit <= of <= 8 (for example \"2/4\").";
        out.condHit = hit;
        out.condOf = of;
    }
    return {};
}

} // namespace

juce::String readNoteOp(const juce::var& noteVar, MidiNote& out) {
    auto* noteObj = noteVar.getDynamicObject();
    if (noteObj == nullptr)
        return "has a note that is not an object.";
    if (const auto bad = firstUnknownKey(*noteObj, {"startBeat", "lengthBeats", "pitch", "velocity", "channel",
                                                    "chance", "repeat", "flam", "nudge", "condition"});
        bad.isNotEmpty())
        return "has a note with an unknown field \"" + bad + "\". A note accepts only " + kAcceptedKeys + ".";

    if (const auto error = readTiming(*noteObj, out); error.isNotEmpty())
        return error;
    if (const auto error = readPitchVelocityChannel(*noteObj, out); error.isNotEmpty())
        return error;
    return readHitSettings(*noteObj, out);
}

} // namespace synth
