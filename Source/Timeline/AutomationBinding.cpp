#include "AutomationBinding.h"

#include "../Modules/ModuleBase.h" // the global findParameterByID(), non-plugin path
#include "../Plugin/Hosting/HostedPluginModule.h"

namespace synth {

namespace {

// True when `param` carries NO persistent id: either it doesn't implement
// juce::HostedAudioProcessorParameter at all (an old format with only integer indices), or it does
// and returns an empty string (JUCE's own VST2 wrapper does exactly this). Both mean the same thing
// to a lane: there is nothing stable to key on, so an index match is the best this parameter can
// ever offer.
bool hasNoStableId(const juce::AudioProcessorParameter* param) noexcept {
    const auto* hosted = dynamic_cast<const juce::HostedAudioProcessorParameter*>(param);
    return hosted == nullptr || hosted->getParameterID().isEmpty();
}

} // namespace

LaneParamResolution resolveLaneParameter(juce::AudioProcessor* processor, const juce::String& paramId,
                                         int paramIndexHint) {
    LaneParamResolution result;
    if (processor == nullptr || paramId.isEmpty())
        return result;

    if (auto* hosted = dynamic_cast<HostedPluginModule*>(processor)) {
        // No live instance (still loading, unloaded, refused, or never installed): there is nothing
        // to verify a parameter against, so every one of this node's lanes orphans rather than
        // silently doing nothing — see the class comment's point 3, including its noted gap.
        if (!hosted->hasInstance()) {
            result.orphaned = true;
            return result;
        }

        if (auto* exact = hosted->findInstanceParameter(paramId)) {
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(exact))
                result.rangedParam = ranged;
            else
                result.hostedParam = exact;
            return result;
        }

        // No exact id match. The index hint is a rescue ONLY when the plugin format has no stable
        // ids at all — never when the hinted index simply carries a DIFFERENT real id, which is a
        // version change having moved the parameter set under us.
        if (paramIndexHint >= 0) {
            if (auto* atHint = hosted->findInstanceParameterByIndex(paramIndexHint)) {
                if (hasNoStableId(atHint)) {
                    if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(atHint))
                        result.rangedParam = ranged;
                    else
                        result.hostedParam = atHint;
                    return result;
                }
            }
        }

        // The node is a live, loaded HostedPluginModule and its parameter set cannot produce a safe
        // match: ORPHAN, never a silent bind to whatever now happens to be there.
        result.orphaned = true;
        return result;
    }

    // Non-plugin module: unchanged path. A miss is "unbound", never "orphaned" — paramIndexHint is
    // never consulted here.
    result.rangedParam = findParameterByID(processor, paramId);
    return result;
}

LaneValueBounds laneValueBoundsFor(const LaneParamResolution& resolution) {
    if (resolution.rangedParam != nullptr) {
        const auto& range = resolution.rangedParam->getNormalisableRange();
        return {static_cast<double>(range.start), static_cast<double>(range.end)};
    }
    // A hosted parameter with no NormalisableRange is always normalised (JUCE's own host contract:
    // AudioProcessorParameter::getValue()/setValue() are 0..1 for every format) — see
    // HostedPluginModule.h.
    return {};
}

double laneDefaultValueFor(const LaneParamResolution& resolution) {
    if (resolution.rangedParam != nullptr)
        return static_cast<double>(resolution.rangedParam->convertFrom0to1(resolution.rangedParam->getDefaultValue()));
    if (resolution.hostedParam != nullptr)
        return static_cast<double>(resolution.hostedParam->getDefaultValue());
    return 0.0;
}

int captureParamIndexHint(juce::AudioProcessor* processor, const juce::String& paramId) {
    auto* hosted = dynamic_cast<HostedPluginModule*>(processor);
    return hosted != nullptr ? hosted->getInstanceParamIndexFallback(paramId) : -1;
}

} // namespace synth
