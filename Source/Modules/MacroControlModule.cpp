// MacroControlModule.cpp -- the bank's construction and its per-knob response curves: the curve tables the audio thread
// reads, and the saved state (names and curves). The rest of the module is inline in the header.
#include "MacroControlModule.h"

#include "ModCurve.h"

// One curve per knob: `curve` is the message thread's copy, the tables are the audio thread's hand-off.
struct MacroControlModule::CurveBank {
    struct Slot {
        synth::ModCurve curve;
        juce::SpinLock tableLock;
        synth::ModCurve::Table pendingTable{}; // guarded by tableLock
        std::atomic<bool> pendingDirty{false};
        std::atomic<bool> nonLinear{false};
        synth::ModCurve::Table activeTable{}; // audio thread only
    };
    std::array<Slot, kMaxMacros> slots;
};

MacroControlModule::MacroControlModule()
    : ModuleBase("Macros", kMaxMacros, kMaxMacros) // one knob cable in and one CV out per macro
    , curveBank_(std::make_unique<CurveBank>()) {
    addParameter(countParam =
                     new juce::AudioParameterInt("macroCount", "Knobs", kMinMacros, kMaxMacros, kDefaultMacros));

    for (int i = 0; i < kMaxMacros; ++i)
        addParameter(macroParams[(size_t)i] = new juce::AudioParameterFloat("macro" + juce::String(i + 1), macroName(i),
                                                                            juce::NormalisableRange<float>(0.0f, 1.0f),
                                                                            0.0f, synth::fractionPercentAttributes()));
    // Polarity is per knob (macro1Bipolar .. macro16Bipolar); Edit Layout's per-control panel sets it.
    for (int i = 0; i < kMaxMacros; ++i)
        addParameter(bipolarParams[(size_t)i] =
                         new juce::AudioParameterBool(bipolarParamId(i), macroName(i) + " Bipolar", false));

    addMuteParameter();
    mods_.bind(*this);
}

MacroControlModule::~MacroControlModule() = default;

synth::ModCurve MacroControlModule::getMacroCurve(int index) const {
    return index >= 0 && index < kMaxMacros ? curveBank_->slots[(size_t)index].curve : synth::ModCurve::linear();
}

void MacroControlModule::setMacroCurve(int index, const synth::ModCurve& curve) {
    if (index < 0 || index >= kMaxMacros)
        return;
    auto& slot = curveBank_->slots[(size_t)index];
    if (slot.curve == curve)
        return;
    slot.curve = curve;
    synth::ModCurve::Table table;
    curve.renderTable(table);
    {
        // Blocking is fine here: the audio thread only try-locks, and holds the lock for one 1 KB copy.
        const juce::SpinLock::ScopedLockType lock(slot.tableLock);
        slot.pendingTable = table;
        slot.pendingDirty.store(true, std::memory_order_release);
    }
    slot.nonLinear.store(!curve.isLinear(), std::memory_order_release); // after the table is published
    ++curvesRevision_;
}

float MacroControlModule::shaped(int index, float knob, bool audio) const {
    auto& slot = curveBank_->slots[(size_t)index];
    if (!slot.nonLinear.load(std::memory_order_acquire))
        return knob;
    if (!audio) {
        const juce::SpinLock::ScopedLockType lock(slot.tableLock);
        return synth::ModCurve::lookup(slot.pendingTable, knob);
    }
    if (slot.pendingDirty.load(std::memory_order_acquire)) {
        const juce::SpinLock::ScopedTryLockType lock(slot.tableLock);
        if (lock.isLocked()) {
            slot.activeTable = slot.pendingTable;
            slot.pendingDirty.store(false, std::memory_order_relaxed);
        }
    }
    return synth::ModCurve::lookup(slot.activeTable, knob);
}

juce::var MacroControlModule::getExtraState() const {
    auto* object = new juce::DynamicObject();
    auto* curves = new juce::DynamicObject();
    bool any = false;
    bool anyCurve = false;
    for (int i = 0; i < kMaxMacros; ++i) {
        if (customNames_[(size_t)i].isNotEmpty()) {
            object->setProperty(juce::String(i + 1), customNames_[(size_t)i]);
            any = true;
        }
        const auto& curve = curveBank_->slots[(size_t)i].curve;
        if (!curve.isLinear()) {
            curves->setProperty(juce::String(i + 1), curve.toVar());
            anyCurve = true;
        }
    }
    if (anyCurve)
        object->setProperty("curves", juce::var(curves));
    else
        delete curves;
    if (!any && !anyCurve) {
        delete object;
        return {};
    }
    return juce::var(object);
}

void MacroControlModule::setExtraState(const juce::var& state) {
    std::array<juce::String, kMaxMacros> next{};
    const auto* object = state.getDynamicObject();
    if (object != nullptr)
        for (int i = 0; i < kMaxMacros; ++i) {
            auto cleaned = cleanName(object->getProperty(juce::String(i + 1)).toString());
            next[(size_t)i] = cleaned == macroName(i) ? juce::String() : cleaned;
        }
    if (next != customNames_) {
        customNames_ = next;
        ++namesRevision_;
    }
    const auto curves = object != nullptr ? object->getProperty("curves") : juce::var();
    const auto* saved = curves.getDynamicObject();
    for (int i = 0; i < kMaxMacros; ++i)
        setMacroCurve(i, saved != nullptr && saved->hasProperty(juce::String(i + 1))
                             ? synth::ModCurve::fromVar(saved->getProperty(juce::String(i + 1)))
                             : synth::ModCurve::linear());
}
