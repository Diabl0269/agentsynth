#include "AttenuverterModule.h"

#include "ModCurve.h"

// Kept out of the header: nearly every card reaches this module, and few of them need the curve.
struct AttenuverterModule::CurveState {
    synth::ModCurve curve; // message thread
    juce::SpinLock tableLock;
    synth::ModCurve::Table pendingTable{}; // guarded by tableLock
    std::atomic<bool> pendingDirty{false};
    std::atomic<bool> nonLinear{false};
    synth::ModCurve::Table activeTable{}; // audio thread
};

AttenuverterModule::AttenuverterModule()
    : ModuleBase("Attenuverter", 2, 1) // 1 Audio In + 1 CV In (Amount), 1 Out
    , curveState_(std::make_unique<CurveState>()) {
    addParameter(amountParam = new juce::AudioParameterFloat("amount", "Amount", -1.0f, 1.0f, 0.0f));
    synth::ModCurve::linear().renderTable(curveState_->activeTable);
}

AttenuverterModule::~AttenuverterModule() = default;

synth::ModCurve AttenuverterModule::getCurve() const { return curveState_->curve; }

void AttenuverterModule::setCurve(const synth::ModCurve& curve) {
    curveState_->curve = curve;
    synth::ModCurve::Table table;
    curve.renderTable(table);
    {
        // A blocking lock is fine here: the audio thread only ever try-locks, and holds it for one 1 KB copy.
        const juce::SpinLock::ScopedLockType lock(curveState_->tableLock);
        curveState_->pendingTable = table;
        curveState_->pendingDirty.store(true, std::memory_order_release);
    }
    // After the table is published, so the audio thread never sees "non-linear" without a table to adopt.
    curveState_->nonLinear.store(!curve.isLinear(), std::memory_order_release);
}

juce::var AttenuverterModule::getExtraState() const {
    return curveState_->curve.isLinear() ? juce::var() : curveState_->curve.toVar();
}

void AttenuverterModule::setExtraState(const juce::var& state) { setCurve(synth::ModCurve::fromVar(state)); }

void AttenuverterModule::applyCurve(float* audioData, int numSamples) noexcept {
    auto& state = *curveState_;
    if (!state.nonLinear.load(std::memory_order_acquire))
        return;

    // A failed try-lock (the message thread is mid-publish) keeps the previous table until the next block.
    if (state.pendingDirty.load(std::memory_order_acquire)) {
        const juce::SpinLock::ScopedTryLockType lock(state.tableLock);
        if (lock.isLocked()) {
            state.activeTable = state.pendingTable;
            state.pendingDirty.store(false, std::memory_order_relaxed);
        }
    }

    for (int i = 0; i < numSamples; ++i)
        audioData[i] = synth::ModCurve::lookup(state.activeTable, audioData[i]);
}
