#pragma once

#include "ModuleBase.h"
#include <atomic>
#include <juce_audio_basics/juce_audio_basics.h>

/**
 * @brief "Macro In" — an audio/CV inlet jack on a Macro's boundary (P8-15 Macro I/O,
 * docs/macros.md §5).
 *
 * A pure PASS-THROUGH: whatever lands on a visible input jack reaches the matching output jack
 * unchanged (the graph hands every node its input already sitting in the buffer it wants the
 * output in, so there is nothing to copy). The macro boundary itself stays a rendering concept —
 * buildVisibleCables() anchors a cable crossing a collapsed card to this node's card jack — this
 * module carries no boundary logic of its own.
 *
 * CHANNEL SHAPE (docs/macros.md §5.3): "a port node's channel shape is decided when the node is
 * constructed, and is immutable for that node's lifetime." JUCE settles the bus layout in the
 * ModuleBase constructor, so this node always carries kMaxChannels raw channels — the same
 * declare-a-maximum-and-vary-the-visible-count pattern Audio Input and Hosted Plugin already use
 * (Source/Modules/CLAUDE.md) — and only `visibleChannels_` (persisted via getExtraState, TRUSTED-
 * PATH ONLY like every module's "state") says how many are actually in play: 1 for Mono, 2 for
 * Stereo, up to kMaxChannels for a Poly-N bus. This stage (P8-15a, §7 item 1) only ever
 * constructs the Mono default — nothing yet offers Stereo or Poly-N (that is the port-creation
 * flow, §7 item 3) — but the bus is already sized so that flow never needs a wider one, which
 * would mean a second factory type or a saved-patch migration for every Macro In already on disk.
 * mapInputChannel/mapOutputChannel stay the ModuleBase default (one jack per visible raw channel,
 * PortRole::Other) deliberately: neither the split-block stereo pairing nor the poly-fan single-
 * jack mapping §5.3 describes is reachable yet, and guessing at either now would only have to be
 * redone once the flow that actually creates a Stereo or Poly-N port exists.
 *
 * INTERNAL-ONLY, the same three exclusions as Track In / Rec Tap / Track Audio: not in the module
 * library, not offered by the replace menu, never authorable by a model
 * (kNonAuthorableModuleTypes, docs/macros.md §6).
 *
 * No "muted" parameter, following the same three modules: there is nothing here for a mute to
 * silence beyond what bypass already covers, and it is deliberately not added just because a
 * future macro-level mute fan-out (docs/macros.md §5.6, §7 item 6) might want to call setMuted on
 * every member — that fan-out already has to guard against members with no mute parameter today
 * (any macro can already contain a Track In / Rec Tap / Track Audio node), so this is not a new
 * gap.
 */
class MacroInletModule : public ModuleBase {
public:
    /** Every raw channel the node can ever carry — sized to cover Mono(1)/Stereo(2)/Poly-N up to
     *  this codebase's existing 8-voice convention (PolyMidiModule's per-CV-type fan). Fixed for
     *  the node's lifetime; see the class comment. */
    static constexpr int kMaxChannels = 8;

    MacroInletModule()
        : ModuleBase("Macro In", kMaxChannels, kMaxChannels) {
        enableVisualBuffer(true);
    }

    ~MacroInletModule() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override {
        juce::ignoreUnused(sampleRate, samplesPerBlock);
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override {
        juce::ignoreUnused(midiMessages);
        const int numSamples = buffer.getNumSamples();
        const int visible = getVisibleOutputPortCount();

        // Hidden-channel hygiene, unconditional (bypassed or not): nothing should be wired to a
        // raw channel with no visible jack (GraphEditor::dropRoutingsOnHiddenJacks), but the bus
        // is always kMaxChannels wide, so a hidden channel otherwise carries whatever the graph
        // last left there. Mirrors AudioInputModule's own hidden-channel clear.
        for (int ch = visible; ch < buffer.getNumChannels(); ++ch)
            buffer.clear(ch, 0, numSamples);

        // Bypass: this module HAS a dry audio path — it IS nothing but a pass-through — so the
        // standard two-branch contract (docs/architecture.md) leaves the visible channels
        // untouched either way. isBypassed() changes nothing observable about the signal; the
        // parameter exists (inherited from ModuleBase) so this module honours the same contract
        // every processBlock does, not because there is a dry/processed distinction to make here.
        if (isBypassed())
            return;

        if (auto* vb = getVisualBuffer())
            for (int i = 0; i < numSamples; ++i)
                vb->pushSample(visible > 0 ? buffer.getReadPointer(0)[i] : 0.0f);
    }

    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }

    ModuleType getModuleType() const override { return ModuleType::MacroInlet; }
    ModulationCategory getModulationCategory() const override { return ModulationCategory::Other; }

    int getVisibleInputPortCount() const override { return visibleChannels_.load(std::memory_order_relaxed); }
    int getVisibleOutputPortCount() const override { return visibleChannels_.load(std::memory_order_relaxed); }

    // ---- Non-parameter state (survives undo / preset load; see ModuleBase::getExtraState).
    // TRUSTED-PATH ONLY — AIStateMapper::applyExtraStateToProcessor never calls setExtraState for
    // untrusted (model-authored) JSON, and this type is additionally refused outright on the
    // untrusted path regardless (kNonAuthorableModuleTypes), so there are two independent reasons
    // this can never be reached by a patch suggestion. ----

    juce::var getExtraState() const override {
        auto* obj = new juce::DynamicObject();
        obj->setProperty("channels", visibleChannels_.load(std::memory_order_relaxed));
        return juce::var(obj);
    }

    void setExtraState(const juce::var& state) override {
        if (auto* obj = state.getDynamicObject())
            if (obj->hasProperty("channels"))
                visibleChannels_.store(juce::jlimit(1, kMaxChannels, (int)obj->getProperty("channels")),
                                       std::memory_order_relaxed);
    }

private:
    // Written once — by setExtraState() on a trusted load, or (from P8-15's later stages) by the
    // port-creation flow immediately after construction, before the node is added to a running
    // graph — and never again: §5.3's "immutable for that node's lifetime" rule. Atomic because it
    // is written on the message thread and read every block on the audio thread; relaxed is
    // enough since nothing else depends on ordering against it.
    std::atomic<int> visibleChannels_{1}; // Mono default — the only shape anything constructs today

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MacroInletModule)
};
