#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace synth {

// The ONE place a lane's (nodeUuid, paramId, paramIndexHint) identity is turned into a live
// parameter, once the caller has already resolved nodeUuid to a live juce::AudioProcessor (node
// resolution itself stays wherever each call site already does it — a uuid->processor map, a
// linear scan — this is only about the PARAMETER half). Every resolution call site is meant to
// funnel through here: AudioEngine::publishTimeline's binding build, TimelineReconciler,
// TimelineOps::runWriteLane / TimelineValidator::validateLane, and MainComponent's automation-
// recorder rebind. Keeping it in one function is what makes "how a lane resolves" and "when it
// orphans" identical across the audio path, the AI-tool path and the UI path.
//
// -- Why a hosted plugin needs a different rule than everything else ----------------------------
//
// One of our OWN modules' parameters (juce::AudioParameterFloat/Bool/Choice/...) is a
// RangedAudioParameter: its paramID is assigned once, in code we wrote, and never moves. A HOSTED
// plugin's parameter SET is discovered at load and can change shape between plugin versions — a
// paramID can vanish, or worse, a DIFFERENT parameter can silently appear at the same index. Keying
// on index there would risk automating the wrong knob after an update; the rule below is what
// stops that:
//
//   1. Exact id match on the LIVE instance (HostedPluginModule::findInstanceParameter) always wins
//      when it succeeds — this is a juce::HostedAudioProcessorParameter::getParameterID() match,
//      NOT a RangedAudioParameter::paramID match (see HostedPluginModule.h's class comment: a
//      hosted parameter is a sibling hierarchy with no NormalisableRange).
//   2. Failing that, the lane's stored paramIndexHint (captured at lane-CREATION time, never
//      re-derived) is tried, but ONLY as a rescue for a plugin FORMAT that has no stable ids at all.
//      If the hinted index exists and carries a real id — just a DIFFERENT one — that is a version
//      having moved the parameter set under us, and the lane must ORPHAN rather than bind to
//      whatever now happens to sit there.
//   3. Anything else — no live instance at all (still loading, explicitly unloaded, refused, or
//      never installed), or a hint that no longer names anything — is an ORPHAN. A
//      HostedPluginModule node with no instance cannot currently vouch for ANY parameter, so its
//      lanes surface for the user to notice and re-bind rather than silently doing nothing.
//      KNOWN GAP: nothing currently re-triggers TimelineReconciler::reconcile when an async plugin
//      load COMPLETES (there is no publish -> reconcile hook), so a freshly opened project's
//      hosted-plugin lanes can read orphaned until an unrelated graph change happens to trigger the
//      next reconcile pass. Not addressed here; every existing reconcile call site still re-derives
//      the correct flag whenever it does run.
//
// Non-plugin nodes are the UNCHANGED path: exact paramID match via the global findParameterByID
// (Source/Modules/ModuleBase.h), and a miss there is "unbound", never "orphaned". paramIndexHint
// is never consulted for a non-plugin node.
struct LaneParamResolution {
    // Exactly one of these two is non-null on a successful resolution. `rangedParam` covers every
    // one of our own modules' parameters, and the (currently theoretical) case of a hosted
    // parameter that happens to implement RangedAudioParameter too. `hostedParam` is the normal case
    // for a hosted plugin's own parameter: a plain AudioProcessorParameter with no
    // NormalisableRange.
    juce::RangedAudioParameter* rangedParam = nullptr;
    juce::AudioProcessorParameter* hostedParam = nullptr;

    // True when the lane's node is a live HostedPluginModule instance whose parameter set could NOT
    // produce a safe match (case 3 above, instance-present branch) — the caller must ORPHAN the
    // lane, not merely leave it unbound. False for every other outcome, resolved or not.
    bool orphaned = false;

    bool resolved() const noexcept { return rangedParam != nullptr || hostedParam != nullptr; }

    // The parameter to actually read/write/listen on, whichever field is populated, or nullptr.
    juce::AudioProcessorParameter* liveParameter() const noexcept {
        return rangedParam != nullptr ? static_cast<juce::AudioProcessorParameter*>(rangedParam) : hostedParam;
    }
};

// The [minValue, maxValue] a lane's breakpoints must lie within for a resolved parameter, in the
// parameter's own units: a RangedAudioParameter's NormalisableRange, or exactly [0, 1] for a hosted
// parameter with none (every hosted AudioProcessorParameter's native domain is 0..1 by JUCE's own
// host contract — see HostedPluginModule.h). Undefined or a resolution that didn't resolve should
// not be passed in; callers check resolved() first.
struct LaneValueBounds {
    double minValue = 0.0;
    double maxValue = 1.0;
};

// Message thread. Resolves `paramId` (with `paramIndexHint`, -1 = none, as the hosted-plugin
// fallback described above) against `processor`, which the caller has already resolved from a
// lane's nodeUuid. `processor` may be nullptr (a node that does not resolve at all) — that always
// returns an unresolved, non-orphaned result, matching "the node itself resolves" being a separate
// question every call site already answers its own way.
LaneParamResolution resolveLaneParameter(juce::AudioProcessor* processor, const juce::String& paramId,
                                         int paramIndexHint = -1);

// The value bounds to enforce for `resolution` — see LaneValueBounds. Callers must have already
// checked resolution.resolved().
LaneValueBounds laneValueBoundsFor(const LaneParamResolution& resolution);

// The parameter's default value, denormalised into its own units for rangedParam, or the hosted
// parameter's native (already 0..1) default. Callers must have already checked resolved().
double laneDefaultValueFor(const LaneParamResolution& resolution);

// -1 for anything that is not a live HostedPluginModule instance parameter (a non-plugin
// processor, no instance yet, or `paramId` does not currently resolve) — the value to capture into
// a NEW lane's paramIndexHint at creation time. Never meaningful to call again after that.
int captureParamIndexHint(juce::AudioProcessor* processor, const juce::String& paramId);

} // namespace synth
