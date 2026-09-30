#pragma once

#include <juce_core/juce_core.h>
#include <optional>

namespace synth {

/** Which optional panels a module card has open: its scope, its filter response curve and that
 *  curve's spectrum backdrop. Presentation only -- it never reaches the audio thread -- so it lives
 *  on the module (which outlives its card, and so survives a card rebuild) and is saved as the
 *  node's own `"cardView"` key, separate from `"state"` (which each module type owns the shape of).
 *
 *  Absent means the card's built-in defaults: scope and response closed. The spectrum default is
 *  per card (off on a Filter, on on a Parametric EQ), so it is tri-state: unset follows that default.
 *  Message thread only. */
struct CardViewState {
    bool showScope = false;
    bool showResponse = false;
    std::optional<bool> showSpectrum;

    bool isDefault() const noexcept { return !showScope && !showResponse && !showSpectrum.has_value(); }

    /** A void var when everything is at its default, so an untouched card adds no JSON. */
    juce::var toVar() const {
        if (isDefault())
            return {};
        auto* obj = new juce::DynamicObject();
        if (showScope)
            obj->setProperty("scope", true);
        if (showResponse)
            obj->setProperty("response", true);
        if (showSpectrum.has_value())
            obj->setProperty("spectrum", *showSpectrum);
        return juce::var(obj);
    }

    /** Lenient on purpose: this is presentation state, so a value of the wrong type is skipped rather
     *  than refusing the project. Anything that is not an object reads as the defaults. */
    static CardViewState fromVar(const juce::var& v) {
        CardViewState out;
        if (auto* obj = v.getDynamicObject()) {
            const auto scope = obj->getProperty("scope");
            const auto response = obj->getProperty("response");
            const auto spectrum = obj->getProperty("spectrum");
            out.showScope = scope.isBool() && static_cast<bool>(scope);
            out.showResponse = response.isBool() && static_cast<bool>(response);
            if (spectrum.isBool())
                out.showSpectrum = static_cast<bool>(spectrum);
        }
        return out;
    }
};

} // namespace synth
