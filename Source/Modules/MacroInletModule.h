#pragma once

#include "MacroPortShape.h"
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
 * (Source/Modules/CLAUDE.md) — and `shape_`/`voiceCount_` (persisted via getExtraState, TRUSTED-
 * PATH ONLY like every module's "state") say how those raw channels map to visible jacks:
 *
 *   - Mono  — raw ch0 is the one visible jack.
 *   - Stereo — raw ch0 (Left) and raw kRightBase (Right) are two visible jacks. The right leg
 *     sits on its own block rather than ch1, matching the split-block convention every other
 *     stereo-capable module in this codebase uses (Source/Modules/CLAUDE.md) — there is no CV
 *     here to collide with, but a Macro In is not exempted from the convention on that account.
 *   - Poly  — raw ch0..voiceCount_-1 are ONE visible jack (a poly-bus fan), ch0 the group head.
 *
 * setPortShape() is called exactly ONCE, by the port-creation flow, immediately after
 * construction and before the node is wired into a live graph — never again (§5.3's immutability
 * rule). This is what lets a Stereo/Poly-N port exist with no new factory type and no migration
 * for a Macro In already on disk: the bus was sized for it from P8-15a onward.
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

    /** Raw channel carrying Stereo's right leg. Arbitrary but fixed — this module has no CV
     *  channels to collide with, unlike the voice modules ch1 is reserved for elsewhere, but the
     *  split-block convention (never ch1) is followed anyway for consistency; see the class
     *  comment. Must stay < kMaxChannels and > 1. Numerically the same slot Poly-N's 5th voice
     *  (rawChannel 4) would use — safe only because a single node is exactly one shape for its
     *  whole lifetime (mapChannelForShape switches on `shape_`, never both at once); if shape ever
     *  became mutable in place rather than delete+re-add, this aliasing would need revisiting. */
    static constexpr int kRightBase = 4;

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

        // Hidden-channel hygiene, unconditional (bypassed or not): nothing should be wired to a
        // raw channel the current shape does not expose as a visible jack (GraphEditor drops
        // those routings the moment the shape changes — changeMacroPortShape), but the bus is
        // always kMaxChannels wide, so a hidden channel otherwise carries whatever the graph last
        // left there. Shape-aware (not a plain "clear ch >= visible count" loop): Stereo's active
        // raw channels are {0, kRightBase}, not a contiguous run. Mirrors AudioInputModule's own
        // hidden-channel clear.
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            if (!isRawChannelActive(ch))
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
                vb->pushSample(buffer.getReadPointer(0)[i]); // ch0 is active in every shape
    }

    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }

    ModuleType getModuleType() const override { return ModuleType::MacroInlet; }
    ModulationCategory getModulationCategory() const override { return ModulationCategory::Other; }

    int getVisibleInputPortCount() const override { return visibleJackCount(); }
    int getVisibleOutputPortCount() const override { return visibleJackCount(); }

    // Both directions map the SAME way — this node is a symmetric pass-through, so "raw channel N
    // is active under the current shape" does not depend on which side is asking.
    LogicalPort mapInputChannel(int rawChannel) const override { return mapChannelForShape(rawChannel); }
    LogicalPort mapOutputChannel(int rawChannel) const override { return mapChannelForShape(rawChannel); }

    int rightAudioLegChannel() const override {
        switch (shape_.load(std::memory_order_relaxed)) {
        case MacroPortShape::Stereo:
            return kRightBase;
        case MacroPortShape::StereoCollapsed:
            return 1; // contiguous with ch0, like a collapsed FX module's own right leg
        default:
            return -1;
        }
    }

    // ---- Port-creation flow API (P8-15b, docs/macros.md §7 item 3). Called exactly ONCE, by
    // GraphEditor::addMacroPort/changeMacroPortShape, right after construction and before the
    // node is added to a running graph — never again; see the class comment's immutability rule.
    // `voiceCount` is meaningless (and ignored) unless `shape` is Poly, where it is clamped to
    // [1, kMaxChannels]. ----

    void setPortShape(MacroPortShape shape, int voiceCount = 1) {
        shape_.store(shape, std::memory_order_relaxed);
        voiceCount_.store(juce::jlimit(1, kMaxChannels, voiceCount), std::memory_order_relaxed);
    }
    MacroPortShape getPortShape() const { return shape_.load(std::memory_order_relaxed); }
    int getVoiceCount() const { return voiceCount_.load(std::memory_order_relaxed); }

    // ---- Non-parameter state (survives undo / preset load; see ModuleBase::getExtraState).
    // TRUSTED-PATH ONLY — AIStateMapper::applyExtraStateToProcessor never calls setExtraState for
    // untrusted (model-authored) JSON, and this type is additionally refused outright on the
    // untrusted path regardless (kNonAuthorableModuleTypes), so there are two independent reasons
    // this can never be reached by a patch suggestion. ----

    juce::var getExtraState() const override {
        auto* obj = new juce::DynamicObject();
        obj->setProperty("shape", macroPortShapeToString(shape_.load(std::memory_order_relaxed)));
        obj->setProperty("voices", voiceCount_.load(std::memory_order_relaxed));
        return juce::var(obj);
    }

    void setExtraState(const juce::var& state) override {
        if (auto* obj = state.getDynamicObject()) {
            if (obj->hasProperty("shape"))
                shape_.store(macroPortShapeFromString(obj->getProperty("shape").toString()), std::memory_order_relaxed);
            if (obj->hasProperty("voices"))
                voiceCount_.store(juce::jlimit(1, kMaxChannels, (int)obj->getProperty("voices")),
                                  std::memory_order_relaxed);
        }
    }

private:
    int visibleJackCount() const {
        switch (shape_.load(std::memory_order_relaxed)) {
        case MacroPortShape::Stereo:
            return 2;
        case MacroPortShape::Mono:
        case MacroPortShape::Poly:
        case MacroPortShape::StereoCollapsed:
        default:
            // Mono and Poly are both a single visible jack (Poly's is a fanned bus).
            // StereoCollapsed is ALSO one visible jack, on purpose (MacroPortShape.h's class
            // comment) — it carries two raw channels but mirrors an ordinary collapsed FX jack,
            // which shows one "Audio" jack for both legs.
            return 1;
        }
    }

    LogicalPort mapChannelForShape(int rawChannel) const {
        LogicalPort p; // default: visibleJackIndex 0, role Other, isPolyGroupHead false, span 1
        switch (shape_.load(std::memory_order_relaxed)) {
        case MacroPortShape::Stereo:
            if (rawChannel == 0) {
                p.visibleJackIndex = 0;
                p.role = PortRole::Audio;
                p.isPolyGroupHead = true;
            } else if (rawChannel == kRightBase) {
                p.visibleJackIndex = 1;
                p.role = PortRole::Audio;
                p.isPolyGroupHead = true;
            }
            break;
        case MacroPortShape::StereoCollapsed:
            // Mirrors ModuleBase::mapStereoPairOutput's collapsed (Dual I/O off) branch exactly:
            // ONE visible jack, raw ch0 the poly-group head with span 2 (both legs), raw ch1 the
            // silent follower — contiguous, unlike Stereo's split-block ch0/kRightBase pair, since
            // GraphEditor's JackTarget expansion assumes rawHeadChannel..+span-1 is adjacent.
            if (rawChannel == 0) {
                p.visibleJackIndex = 0;
                p.role = PortRole::Audio;
                p.isPolyGroupHead = true;
                p.polyVoiceSpan = 2;
            } else if (rawChannel == 1) {
                p.visibleJackIndex = 0;
                p.role = PortRole::Audio;
                p.isPolyGroupHead = false;
                p.polyVoiceSpan = 1;
            }
            break;
        case MacroPortShape::Poly: {
            const int voices = juce::jlimit(1, kMaxChannels, voiceCount_.load(std::memory_order_relaxed));
            if (rawChannel < voices) {
                p.visibleJackIndex = 0;
                p.role = PortRole::Audio;
                p.isPolyGroupHead = (rawChannel == 0);
                p.polyVoiceSpan = voices;
            }
            break;
        }
        case MacroPortShape::Mono:
        default:
            if (rawChannel == 0) {
                p.visibleJackIndex = 0;
                p.role = PortRole::Audio;
                p.isPolyGroupHead = true;
            }
            break;
        }
        return p;
    }

    bool isRawChannelActive(int rawChannel) const { return mapChannelForShape(rawChannel).role == PortRole::Audio; }

    // Written once — by setExtraState() on a trusted load, or by setPortShape() from the
    // port-creation flow immediately after construction, before the node is added to a running
    // graph — and never again: §5.3's "immutable for that node's lifetime" rule. Atomic because it
    // is written on the message thread and read every block on the audio thread; relaxed is
    // enough since nothing else depends on ordering against it.
    std::atomic<MacroPortShape> shape_{MacroPortShape::Mono}; // Mono default — matches every save
    std::atomic<int> voiceCount_{1};                          // meaningful only when shape_ == Poly

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MacroInletModule)
};
