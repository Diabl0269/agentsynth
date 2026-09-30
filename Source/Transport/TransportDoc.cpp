#include "Transport/TransportDoc.h"
#include "Transport/TransportService.h"
#include <cmath>

namespace synth {

namespace {
constexpr const char* kBpm = "bpm";
constexpr const char* kTimeSigNum = "timeSigNum";
constexpr const char* kTimeSigDen = "timeSigDen";
constexpr const char* kLoopStart = "loopStartBeat";
constexpr const char* kLoopEnd = "loopEndBeat";
constexpr const char* kLoopEnabled = "loopEnabled";

bool readNumber(const juce::DynamicObject& obj, const char* key, double& out) {
    const auto v = obj.getProperty(key);
    if (!(v.isDouble() || v.isInt() || v.isInt64()))
        return false;
    out = static_cast<double>(v);
    return std::isfinite(out);
}

bool readInt(const juce::DynamicObject& obj, const char* key, int& out) {
    double d = 0.0;
    if (!readNumber(obj, key, d) || d != std::floor(d) || d < 0.0 || d > 1.0e6)
        return false;
    out = static_cast<int>(d);
    return true;
}

bool isNoteValueDenominator(int d) { return d == 1 || d == 2 || d == 4 || d == 8 || d == 16 || d == 32; }
} // namespace

juce::var TransportDoc::toVar() const {
    auto* obj = new juce::DynamicObject();
    obj->setProperty(kBpm, bpm);
    obj->setProperty(kTimeSigNum, timeSigNumerator);
    obj->setProperty(kTimeSigDen, timeSigDenominator);
    obj->setProperty(kLoopStart, loopStartBeat);
    obj->setProperty(kLoopEnd, loopEndBeat);
    obj->setProperty(kLoopEnabled, loopEnabled);
    return juce::var(obj);
}

// Strict on purpose: the same range rules TransportService's setters enforce, so a loaded document can
// never ask the transport for something its own API would refuse.
bool TransportDoc::fromVar(const juce::var& v) {
    auto* obj = v.getDynamicObject();
    if (obj == nullptr)
        return false;

    TransportDoc local;
    if (!readNumber(*obj, kBpm, local.bpm) || !readInt(*obj, kTimeSigNum, local.timeSigNumerator) ||
        !readInt(*obj, kTimeSigDen, local.timeSigDenominator) || !readNumber(*obj, kLoopStart, local.loopStartBeat) ||
        !readNumber(*obj, kLoopEnd, local.loopEndBeat))
        return false;

    const auto enabled = obj->getProperty(kLoopEnabled);
    if (!enabled.isBool())
        return false;
    local.loopEnabled = static_cast<bool>(enabled);

    if (local.bpm < TransportService::kMinBpm || local.bpm > TransportService::kMaxBpm)
        return false;
    if (local.timeSigNumerator < 1 || local.timeSigNumerator > 64 || !isNoteValueDenominator(local.timeSigDenominator))
        return false;
    if (local.loopStartBeat < 0.0 || local.loopEndBeat < 0.0)
        return false;
    if (local.loopEnabled && local.loopEndBeat - local.loopStartBeat < TransportService::kMinLoopLengthBeats)
        return false;

    *this = local;
    return true;
}

bool TransportDoc::operator==(const TransportDoc& o) const noexcept {
    return bpm == o.bpm && timeSigNumerator == o.timeSigNumerator && timeSigDenominator == o.timeSigDenominator &&
           loopStartBeat == o.loopStartBeat && loopEndBeat == o.loopEndBeat && loopEnabled == o.loopEnabled;
}

} // namespace synth
