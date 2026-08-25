#pragma once

#include <atomic>

namespace synth::harness {

// Hard ceiling on outbound model requests for one harness invocation (P1-11). A scenario's
// applyPatchWithRetry() correction round-trips issue additional sendPrompt() calls beyond the
// initial one, so this must be enforced at the single chokepoint every request passes through
// (RecordingProvider::sendPrompt in Main.cpp), not by counting scenarios — a scenario-based cap
// would undercount and let a retry storm run past the limit anyway.
//
// This exists specifically for the case where sendPrompt() reaches a paid vendor (--provider
// remote, routed through AWS Bedrock): a runaway loop must not be able to bill past a known
// ceiling. It is a no-op safety net for the free --provider ollama path.
class RequestBudget {
public:
    // limit <= 0 means unlimited (tryConsume() always succeeds) — the harness's default for
    // --provider ollama, where a request costs nothing but electricity.
    explicit RequestBudget(int limit)
        : limitValue(limit) {}

    // Call once per outbound request, before it is allowed to reach the provider. Returns false
    // once the limit has been reached; every request after that must be refused without ever
    // calling the wrapped provider.
    bool tryConsume() {
        if (limitValue <= 0)
            return true;
        return ++usedValue <= limitValue;
    }

    int used() const { return usedValue; }
    int limit() const { return limitValue; }

private:
    int limitValue;
    std::atomic<int> usedValue{0};
};

} // namespace synth::harness
