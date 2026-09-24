// Message thread. Arms/cancels a learn, tallies the learnCandidate events the MIDI path pushes
// while one is armed, and settles it 300 ms after the first eligible message onto the message key
// seen most often (docs/control/midi-remote.md#learn-what-does-the-first-message-mean).

#include "MidiRemote/RemoteEngine/RemoteEngine.h"

#include <algorithm>
#include <iterator>
#include <juce_core/juce_core.h>
#include <limits>

namespace synth::midi {

void RemoteEngine::armLearn(const LearnRequest& request) {
    learnRequest_ = request;
    learnTallies_.clear();
    learnHasFirstEvent_ = false;
    learnFirstEventMs_ = 0.0;
    learnArmedMs_ = clock_();

    const std::uint32_t token = nextLearnToken_;
    nextLearnToken_ = token == std::numeric_limits<std::uint32_t>::max() ? 1 : token + 1; // skip 0 on wrap
    learnToken_.store(token, std::memory_order_seq_cst);

    updateTimerState();
}

void RemoteEngine::cancelLearn() {
    learnToken_.store(0, std::memory_order_seq_cst);
    learnTallies_.clear();
}

void RemoteEngine::noteLearnCandidate(const juce::String& sourceKey, const RemoteEvent& event) {
    if (!isLearnArmed())
        return;

    if (!learnHasFirstEvent_) {
        learnHasFirstEvent_ = true;
        learnFirstEventMs_ = clock_();
    }

    MessageSpec spec;
    spec.type = static_cast<MessageType>(event.specType);
    spec.channel = event.specChannel;
    spec.number = event.specNumber;

    auto found = std::find_if(learnTallies_.begin(), learnTallies_.end(), [&](const LearnTally& tally) {
        return tally.sourceKey == sourceKey && tally.spec == spec;
    });
    if (found == learnTallies_.end()) {
        learnTallies_.push_back(LearnTally{sourceKey, spec, 0, false, 1.0f, 0.0f, false});
        found = std::prev(learnTallies_.end());
    }

    // A value of 0 following a non-zero one for this key means the hardware returns to rest on
    // release -- the momentary signature (docs/control/midi-remote.md#learn-what-does-the-first-message-mean).
    if (event.value == 0.0f && found->maxValue > 0.0f)
        found->sawRelease = true;

    // CC only: rawNormalisedValue() is rawValue/127.0f exactly, so a genuine 0 or 127 raw value
    // round-trips to exactly 0.0f/1.0f in IEEE-754 float division -- anything else is a real
    // intermediate value, i.e. a sweep rather than a button tap (FRO130's buttonLike preference
    // below).
    if (spec.type == MessageType::cc && event.value != 0.0f && event.value != 1.0f)
        found->sawIntermediateValue = true;

    ++found->count;
    found->minValue = juce::jmin(found->minValue, event.value);
    found->maxValue = juce::jmax(found->maxValue, event.value);
}

bool RemoteEngine::looksButtonLike(const LearnTally& tally) noexcept {
    if (tally.spec.type == MessageType::note)
        return true;
    return tally.spec.type == MessageType::cc && !tally.sawIntermediateValue;
}

void RemoteEngine::settleLearnIfDue() {
    if (!isLearnArmed())
        return;

    const double now = clock_();

    if (!learnHasFirstEvent_) {
        if (now - learnArmedMs_ >= kLearnTimeoutMs)
            cancelLearn();
        return;
    }

    if (now - learnFirstEventMs_ < kLearnSettleMs)
        return;

    // The tally with the highest count wins; ties keep the first seen (strict '>' never replaces
    // an equal count). A button-like learn (bool param / action) prefers a button-shaped tally
    // over a higher-count sweep that arrived in the same settle window -- e.g. a knob wobbled
    // while reaching for the intended pad (docs/control/midi-remote.md#learn-what-does-the-first-message-mean)
    // -- but only among tallies that actually look like a button; falls through to the plain
    // highest-count rule when none do.
    const LearnTally* best = nullptr;
    if (learnRequest_.buttonLike) {
        for (const auto& tally : learnTallies_)
            if (looksButtonLike(tally) && (best == nullptr || tally.count > best->count))
                best = &tally;
    }
    if (best == nullptr) {
        for (const auto& tally : learnTallies_)
            if (best == nullptr || tally.count > best->count)
                best = &tally;
    }

    LearnResult result;
    const bool haveResult = best != nullptr;
    if (haveResult) {
        result.sourceKey = best->sourceKey;
        result.spec = best->spec;
        // An nrpn is always a 14-bit value pair; a learned CC stays plain absolute (its LSB partner is
        // paired by Detect / the inspector, never inferred from one learn).
        result.encoding = best->spec.type == MessageType::nrpn ? Encoding::abs14 : Encoding::abs7;
        result.buttonMode = best->sawRelease ? ButtonMode::momentary : ButtonMode::toggle;
        result.target = learnRequest_.target;
    }

    learnToken_.store(0, std::memory_order_seq_cst);
    learnTallies_.clear();

    if (haveResult && onLearned)
        onLearned(result);
}

} // namespace synth::midi
