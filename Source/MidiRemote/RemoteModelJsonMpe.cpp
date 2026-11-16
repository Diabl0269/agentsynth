// Concern: the MPE override's JSON dialect and forced-zone channel mask (MpeSettings in RemoteModel.h).
#include "RemoteModel.h"

namespace synth {

namespace {

const char* modeToString(MpeSettings::Mode m) {
    switch (m) {
    case MpeSettings::Mode::automatic:
        return "auto";
    case MpeSettings::Mode::off:
        return "off";
    case MpeSettings::Mode::lower:
        return "lower";
    case MpeSettings::Mode::upper:
        return "upper";
    case MpeSettings::Mode::both:
        return "both";
    }
    return "auto";
}

bool modeFromString(const juce::String& s, MpeSettings::Mode& out) {
    for (const auto m : {MpeSettings::Mode::automatic, MpeSettings::Mode::off, MpeSettings::Mode::lower,
                         MpeSettings::Mode::upper, MpeSettings::Mode::both}) {
        if (s == modeToString(m)) {
            out = m;
            return true;
        }
    }
    return false;
}

// A missing count is the default; a present one must be an int in 1..15.
bool readMemberCount(const juce::var& v, int& out) {
    if (v.isVoid()) {
        out = MpeSettings::kMaxMembers;
        return true;
    }
    if (!v.isInt())
        return false;
    const int n = static_cast<int>(v);
    if (n < 1 || n > MpeSettings::kMaxMembers)
        return false;
    out = n;
    return true;
}

} // namespace

std::uint16_t MpeSettings::forcedMemberMask() const noexcept {
    std::uint16_t mask = 0;
    const bool hasLower = mode == Mode::lower || mode == Mode::both;
    const bool hasUpper = mode == Mode::upper || mode == Mode::both;
    if (hasLower)
        for (int channel = 2; channel <= 1 + lowerMembers; ++channel)
            mask = static_cast<std::uint16_t>(mask | (1u << (channel - 1)));
    if (hasUpper)
        for (int channel = 15; channel >= 16 - upperMembers; --channel)
            mask = static_cast<std::uint16_t>(mask | (1u << (channel - 1)));
    return mask;
}

juce::var MpeSettings::toVar() const {
    auto* obj = new juce::DynamicObject();
    obj->setProperty("mode", modeToString(mode));
    obj->setProperty("lowerMembers", lowerMembers);
    obj->setProperty("upperMembers", upperMembers);
    return juce::var(obj);
}

bool MpeSettings::fromVar(const juce::var& v, MpeSettings& out) {
    auto* obj = v.getDynamicObject();
    if (obj == nullptr)
        return false;

    MpeSettings parsed;
    const auto modeVar = obj->getProperty("mode");
    if (!modeVar.isVoid() && (!modeVar.isString() || !modeFromString(modeVar.toString(), parsed.mode)))
        return false;
    if (!readMemberCount(obj->getProperty("lowerMembers"), parsed.lowerMembers) ||
        !readMemberCount(obj->getProperty("upperMembers"), parsed.upperMembers))
        return false;
    if (parsed.mode == Mode::both && parsed.lowerMembers + parsed.upperMembers > kMaxBothMembers)
        return false;

    out = parsed;
    return true;
}

} // namespace synth
