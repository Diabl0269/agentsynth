#pragma once

#include "ModTargetList.h"
#include "ModuleBase.h"
#include "Modules/ParameterText.h"
#include <array>
#include <atomic>
#include <juce_core/juce_core.h>
#include <memory>

/**
 * Macro Control — a bank of assignable CV knobs.
 *
 * Each macro emits a steady CV on its own output jack. A single jack can feed any number of
 * destinations: the graph wraps every CV cable in its own Attenuverter, so one knob sweeps many
 * parameters at once with independent depth and polarity per destination. That is the whole macro
 * idea — the bank does not need N-to-M routing of its own, the smart cables already provide it.
 *
 * Channel layout: output channel i carries macro i+1. All kMaxMacros channels exist for the
 * lifetime of the node — JUCE fixes the bus layout at construction, and rebuilding it would drop
 * every existing graph connection — so the visible bank size is a *parameter* (`macroCount`).
 * Channels at or above it are cleared every block and hidden by getVisibleOutputPortCount().
 *
 * Each knob also has its own response curve (`setMacroCurve`): the knob's 0..1 position goes through the curve first,
 * and a Bi knob then maps that to -1..1, so the curve shapes what reaches the cables. Linear costs the audio thread
 * nothing; a bent one is one table lookup per block per knob (published by SpinLock try-lock, no allocation).
 *
 * Every macro knob takes a cable too (kModTargets, no jack on the card): input channel i moves knob i, read at the
 * block's first sample before that channel is overwritten with the macro's own output.
 */
namespace synth {
struct ModCurve;
}

class MacroControlModule : public ModuleBase {
public:
    static constexpr int kMaxMacros = 16;
    static constexpr int kMinMacros = 1;
    static constexpr int kDefaultMacros = 8;
    /** Macro i+1's knob cable is input channel i. Append only. */
    static constexpr std::array<synth::ModTargetSpec, kMaxMacros> kModTargets{{
        {"macro1", synth::ModRead::Live},
        {"macro2", synth::ModRead::Live},
        {"macro3", synth::ModRead::Live},
        {"macro4", synth::ModRead::Live},
        {"macro5", synth::ModRead::Live},
        {"macro6", synth::ModRead::Live},
        {"macro7", synth::ModRead::Live},
        {"macro8", synth::ModRead::Live},
        {"macro9", synth::ModRead::Live},
        {"macro10", synth::ModRead::Live},
        {"macro11", synth::ModRead::Live},
        {"macro12", synth::ModRead::Live},
        {"macro13", synth::ModRead::Live},
        {"macro14", synth::ModRead::Live},
        {"macro15", synth::ModRead::Live},
        {"macro16", synth::ModRead::Live},
    }};

    MacroControlModule();
    ~MacroControlModule() override;

    void prepareToPlay(double sampleRate, int /*samplesPerBlock*/) override {
        currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
        for (int i = 0; i < kMaxMacros; ++i) {
            smoothed[(size_t)i].reset(currentSampleRate, 0.02);
            smoothed[(size_t)i].setCurrentAndTargetValue(outputFor(i));
        }
    }

    void processModuleBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override {
        juce::ignoreUnused(midiMessages);

        const int numSamples = buffer.getNumSamples();
        const int numChannels = buffer.getNumChannels();
        if (numSamples == 0 || numChannels == 0)
            return;

        // Pure source module: there is no audio input, so bypass has no dry signal to pass
        // through and clears like mute (same exception as Oscillator / LFO / Noise).
        if (isBypassed() || isMuted()) {
            buffer.clear();
            return;
        }

        const int active = juce::jmin(countParam->get(), numChannels);

        for (int i = 0; i < active; ++i) {
            smoothed[(size_t)i].setTargetValue(outputFor(i, blockCV(buffer, i)));
            auto* out = buffer.getWritePointer(i);
            for (int s = 0; s < numSamples; ++s)
                out[s] = smoothed[(size_t)i].getNextValue();
        }

        // Keep hidden macros tracking their knob so re-enabling them does not ramp from a stale
        // value, then silence their channels.
        for (int i = active; i < kMaxMacros; ++i)
            smoothed[(size_t)i].setCurrentAndTargetValue(outputFor(i));

        for (int ch = active; ch < numChannels; ++ch)
            buffer.clear(ch, 0, numSamples);
    }

    //==============================================================================
    // Port model
    //==============================================================================

    int getVisibleInputPortCount() const override { return 0; }
    juce::String getInputPortLabel(int i) const override { return mods_.label(i); }
    LogicalPort mapInputChannel(int rawChannel) const override { return mods_.portOf(rawChannel); }
    std::vector<ModulationTarget> getModulationTargets() const override { return mods_.targets(); }
    int getVisibleOutputPortCount() const override { return countParam->get(); }

    juce::String getOutputPortLabel(int i) const override {
        return (i >= 0 && i < kMaxMacros) ? getMacroName(i) : ModuleBase::getOutputPortLabel(i);
    }

    LogicalPort mapOutputChannel(int rawChannel) const override {
        LogicalPort p;
        const int visible = getVisibleOutputPortCount();
        p.visibleJackIndex = (visible > 0) ? juce::jlimit(0, visible - 1, rawChannel) : 0;
        p.role = PortRole::ModCV;
        p.isPolyGroupHead = (rawChannel < visible);
        p.polyVoiceSpan = 1;
        return p;
    }

    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }

    ModulationCategory getModulationCategory() const override { return ModulationCategory::Other; }
    /** Per output channel: macro i+1's own polarity. The whole-module answer is "any knob is bipolar". */
    bool isModSourceBipolarAt(int outputChannel) const { return isBipolar(outputChannel); }
    bool isModSourceBipolar() const override {
        for (int i = 0; i < kMaxMacros; ++i)
            if (isBipolar(i))
                return true;
        return false;
    }
    ModuleType getModuleType() const override { return ModuleType::MacroControl; }

    //==============================================================================
    // UI accessors
    //==============================================================================

    /** Number of macros currently exposed as knobs and output jacks. */
    int getMacroCount() const { return countParam->get(); }

    /** Raw knob position (always 0..1, regardless of bipolar). */
    float getMacroKnob(int index) const {
        return (index >= 0 && index < kMaxMacros) ? macroParams[(size_t)index]->get() : 0.0f;
    }

    /** The CV this macro emits — 0..1 unipolar, -1..1 when this knob is bipolar. */
    float getMacroOutput(int index) const {
        return (index >= 0 && index < kMaxMacros) ? outputFor(index, 0.0f, false) : 0.0f;
    }

    bool isBipolar(int index) const { return index >= 0 && index < kMaxMacros && bipolarParams[(size_t)index]->get(); }

    /** Knob `index`'s response curve (linear until set). Message thread only. */
    synth::ModCurve getMacroCurve(int index) const;
    /** Sets knob `index`'s response curve and publishes its table to the audio thread (no allocation there). Message
     *  thread only. */
    void setMacroCurve(int index, const synth::ModCurve& curve);
    /** Bumped whenever a curve changes (a set, an undo, a load); the card polls it to redraw its glyphs. */
    int getCurvesRevision() const noexcept { return curvesRevision_.load(); }

    /** Parameter id of macro `index`'s polarity ("macro1Bipolar" ...). */
    static juce::String bipolarParamId(int index) { return "macro" + juce::String(index + 1) + "Bipolar"; }
    /** True for any of the sixteen polarity parameter ids. */
    static bool isBipolarParamId(const juce::String& id) {
        for (int i = 0; i < kMaxMacros; ++i)
            if (id == bipolarParamId(i))
                return true;
        return false;
    }
    /** The parameter that holds macro `index`'s polarity (null out of range). */
    juce::AudioParameterBool* getBipolarParameter(int index) const {
        return index >= 0 && index < kMaxMacros ? bipolarParams[(size_t)index] : nullptr;
    }

    /** Default name of macro `index` ("M1" … "M16"); also the slider ComponentID used by the UI. */
    static juce::String macroName(int index) { return "M" + juce::String(index + 1); }

    /** The name macro `index` shows: the card caption, its output jack and the AI's state. Its own name, else
     *  `macroName`. Message thread only. */
    juce::String getMacroName(int index) const {
        if (index < 0 || index >= kMaxMacros)
            return {};
        const auto& custom = customNames_[(size_t)index];
        return custom.isEmpty() ? macroName(index) : custom;
    }
    /** Names macro `index`; blank or the default name puts the default back. Cleaned like untrusted text
     *  (trimmed, control characters dropped, at most kMaxNameLength characters). */
    void setMacroName(int index, const juce::String& name) {
        if (index < 0 || index >= kMaxMacros)
            return;
        auto cleaned = cleanName(name);
        if (cleaned == macroName(index))
            cleaned = {};
        if (customNames_[(size_t)index] == cleaned)
            return;
        customNames_[(size_t)index] = cleaned;
        ++namesRevision_;
    }
    /** Bumped whenever a name changes (a set, an undo, a load); the card polls it to refresh its captions. */
    int getNamesRevision() const noexcept { return namesRevision_.load(); }
    static constexpr int kMaxNameLength = 24;

    /** The names as saved: an object keyed "1".."16" holding only the names that differ from the default, and
     *  "curves" (the same keys) holding only the curves that are not linear; void when there are neither, so a bank
     *  nobody renamed or bent adds nothing to a project. */
    juce::var getExtraState() const override;
    void setExtraState(const juce::var& state) override;
    /** The names are short plain text, cleaned the same way, and a curve is pure data that `ModCurve::fromVar` clamps
     *  (anything unusable becomes linear), so a model-authored patch may name and bend the knobs. */
    void setUntrustedExtraState(const juce::var& state) override { setExtraState(state); }

    /** Projects saved before per-knob polarity had one Bipolar switch for the whole bank: on means every knob is. */
    void adoptLegacyParams(const juce::NamedValueSet& params) override {
        if (const auto* legacy = params.getVarPointer("macroBipolar"); legacy != nullptr && legacyOn(*legacy))
            for (auto* bipolar : bipolarParams)
                *bipolar = true;
    }
    bool isLegacyParamId(const juce::String& id) const override { return id == "macroBipolar"; }

    /** The saved-state blob a snapshot or preset carried: the same legacy switch, read from its property. */
    void setStateInformation(const void* data, int sizeInBytes) override {
        ModuleBase::setStateInformation(data, sizeInBytes);
        if (auto xml = getXmlFromBinary(data, sizeInBytes); xml != nullptr && xml->hasTagName("ModuleState")) {
            const auto state = juce::ValueTree::fromXml(*xml);
            if (state.hasProperty("macroBipolar") && (float)state.getProperty("macroBipolar") > 0.5f)
                for (auto* bipolar : bipolarParams)
                    *bipolar = true;
        }
    }

private:
    static bool legacyOn(const juce::var& value) { return value.isBool() ? (bool)value : (double)value > 0.5; }

    /** Printable text only (no control characters), trimmed, at most kMaxNameLength characters. */
    static juce::String cleanName(const juce::String& raw) {
        juce::String kept;
        for (auto c : raw)
            if (juce::CharacterFunctions::isPrintable(c))
                kept += c;
        return kept.trim().substring(0, kMaxNameLength).trim();
    }

    /** The knob position after its curve: the audio thread adopts a published table (a failed try-lock keeps the old
     * one until the next block); the UI's read uses the newest table. */
    float shaped(int index, float knob, bool audio) const;

    float outputFor(int index, float cv = 0.0f, bool audio = true) const {
        const float knob = modulateNormalised(*macroParams[(size_t)index], macroParams[(size_t)index]->get(), cv);
        const float curved = shaped(index, juce::jlimit(0.0f, 1.0f, knob), audio);
        return bipolarParams[(size_t)index]->get() ? (curved * 2.0f - 1.0f) : curved;
    }

    juce::AudioParameterInt* countParam = nullptr;
    std::array<juce::AudioParameterBool*, kMaxMacros> bipolarParams{};
    std::array<juce::String, kMaxMacros> customNames_{};
    std::atomic<int> namesRevision_{0};
    std::atomic<int> curvesRevision_{0};
    struct CurveBank;
    std::unique_ptr<CurveBank> curveBank_;
    std::array<juce::AudioParameterFloat*, kMaxMacros> macroParams{};
    synth::ModTargetList mods_{kModTargets};
    std::array<juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>, kMaxMacros> smoothed{};

    double currentSampleRate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MacroControlModule)
};
