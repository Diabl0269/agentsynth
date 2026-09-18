#pragma once

#include "../Transport/TransportService.h"
#include "Mixer/PeakMeterLatch.h"
#include "ModuleBase.h"
#include <array>
#include <atomic>
#include <juce_audio_basics/juce_audio_basics.h>

/**
 * @brief "Channel Strip" — the end of one mixer channel (P9-2, docs/mixer/mixer.md#node-types).
 *
 * The node the mixer enumerates: a channel's chain terminates here, and the strip's gain, pan,
 * mute and solo are what a mixer column drives. Output is always stereo (Left on ch0, Right on
 * kRightBase); the INPUT is Mono or Stereo, decided once when the strip is created.
 *
 * CHANNEL SHAPE (docs/mixer/mixer.md#mono-and-stereo). JUCE settles the bus layout in the ModuleBase constructor,
 * so the node always carries kNumInputs raw input channels and kNumOutputs raw output channels,
 * and `shape_` says which input jacks are visible:
 *
 *   - Mono   — raw ch0 is the one input jack; the strip feeds both output legs from it.
 *   - Stereo — raw ch0 (Left) and raw kRightBase (Right) are the two input jacks.
 *
 * The right leg sits on its own kRightBase block, never ch1 (Source/Modules/CLAUDE.md). ch1..3 are
 * reserved — nothing is mapped there today, and they are cleared every block so a future gain/pan
 * CV input can take one without repointing a saved patch's right-leg cable. kRightBase matches the
 * Macro In/Out port nodes' own, so a stereo Macro Out wires into a strip 0->0, 4->4.
 *
 * The shape is written ONCE — by setShape() from the channel-creation flow, or by a trusted
 * setExtraState() on load — and is locked from then on (and from the first prepareToPlay(), i.e.
 * once the node is live in a graph). A later write with a different shape is refused: changing a
 * strip's width means replacing the strip, one undo step, never widening a live node.
 *
 * SENDS (FRO15/P9-9, docs/mixer/sends-and-buses.md). A send is not a node: it is a strip-owned OUTPUT leg,
 * a real stereo pair of raw output channels an ordinary graph edge carries into a bus strip's
 * input. kMaxSends fixed slots are declared at construction (a variable-port module declares its
 * maximum and varies only the VISIBLE count — Source/Modules/CLAUDE.md), each with its own L and R
 * block so the "right leg on its own block" invariant holds per send:
 *
 *   inputs   0 = In/Left, 1..3 reserved, kRightBase = Right                     -> kNumInputs  = 5
 *   main out 0 = Left,    1..3 reserved, kRightBase = Right, 5..7 reserved
 *   send L   kSendBase + slot                                                   (8..11)
 *   send R   kSendBase + kMaxSends + slot                                       (12..15)
 *                                                                               -> kNumOutputs = 16
 *
 * Slots are SPARSE and addressed by slot index: removing a middle send clears its bit and leaves
 * every higher slot on its own raw channels, so no cable is ever re-wired and no parameter value is
 * ever copied. Only the VISIBLE jack indices renumber (mapOutputChannel counts active slots below
 * the one being mapped); the jack LABEL keeps naming the slot ("Send 3 L" stays Send 3), so a jack,
 * its mixer row and its `send3Level` parameter always agree.
 *
 * All kMaxSends level parameters are added in the constructor UNCONDITIONALLY, active or not:
 * adding a parameter later renumbers the host-visible layout and detaches saved host automation.
 * Which slots actually exist, and each slot's pre/post choice, are non-parameter trusted extra
 * state ("sends"); the send's TARGET is never stored — it is the graph edge itself, since node ids
 * are reassigned on every rebuild-from-JSON and a stored id would go stale on undo.
 *
 * Pre-fader is tapped after the hygiene/mono duplication and BEFORE gain and pan; post-fader after
 * them, i.e. exactly the signal the strip hands Master, and before the solo gate. Under bypass the
 * strip's own gain and pan are off, so pre and post coincide. MUTE SILENCES SENDS TOO — the mute
 * branch stays `buffer.clear()` per the root CLAUDE.md two-branch contract, a deliberate departure
 * from DAWs that keep a pre-fader cue alive under mute.
 *
 * The (5, 16) channel shape does not match hasStereoOutputPairShape, so no Dual I/O toggle is
 * inherited and none is wanted — the strip owns its own fixed jack map below.
 *
 * SOLO (docs/mixer/mixer.md#solo-is-a-render-time-gate). `soloed_` is NOT an AudioParameter: not host-visible, not
 * automatable, persisted only in the trusted extra state. Whether ANY strip is soloed is an
 * engine-owned count (AudioEngine::refreshSoloGate) carried to the audio thread on the playhead —
 * TransportService::isMixerSoloActiveForBlock(). While it is set, the gate is applied PER LEG
 * rather than to the whole strip: `soloAudibleMask_` (published by refreshSoloGate from
 * synth::computeSoloAudibleLegs) carries one bit for the main legs and one per send slot, and
 * processBlock silences exactly the legs whose bit is closed. That is what makes "solo the reverb
 * bus" mean the sources' SEND legs stay open while their dry main legs do not, and "solo a source"
 * keep the buses it feeds audible. Solo is never a setMuted() fan-out.
 *
 * The mask defaults to 0 (nothing audible) on purpose: a strip the engine has not published a mask
 * for behaves exactly as the pre-send whole-buffer clear did — silent while another strip is
 * soloed — so a missed publication can never leak into a soloed mix. A strip that is ITSELF soloed
 * short-circuits the mask entirely, which is what keeps a bare module (no engine, no publication)
 * correct in a headless test.
 *
 * BYPASS / MUTE. Two separate branches, per the root CLAUDE.md contract:
 *   - bypass disables the strip's OWN gain and pan (dry pass-through; a mono strip still feeds
 *     both output legs, because the output is stereo in every shape);
 *   - mute clears the buffer.
 * The solo gate is applied in BOTH the dry and the normal branch. That is not the forbidden
 * `isBypassed() || isMuted()` collapse: bypass is about this module's own processing, while the
 * solo gate is an engine-level mixer decision layered on top of whatever the strip outputs —
 * a bypassed non-soloed strip leaking into a soloed mix would break docs/mixer/mixer.md#solo-is-a-render-time-gate's "every non-soloed strip
 * outputs silence".
 *
 * STEM TAP (P9-8, docs/mixer/stem-export.md). An opt-in, non-owning tap for offline stem export: a
 * message-thread-armed pointer to a caller-owned stereo destination buffer, null during normal/live
 * playback. When non-null, the strip copies its FINAL stereo output — post gain, pan, mute AND solo
 * gate, i.e. exactly what it hands to Master — into the tap at the end of every processBlock exit
 * path (bypass, mute, solo-gated silence, and the normal path alike), so a muted or soloed-out strip
 * during a stem export still taps whatever it actually output (silence). The tap copies the MAIN
 * legs only and never a send leg, which is what keeps a source's stem pre-send and a bus's stem the
 * only place a pre-fader send appears (docs/mixer/stem-export.md). No allocation, no locks: an atomic pointer swap and,
 * when armed, one copyFrom per leg. The destination buffer must already hold at least `numSamples`
 * samples in 2 channels — synth::StemSession preallocates one per strip at the render's block size
 * before arming any tap — and a block wider than that is dropped rather than overrun, since the
 * audio thread must never touch memory it wasn't handed room for.
 *
 * INTERNAL-ONLY, the same three exclusions as Rec Tap and the macro port types: no library row, no
 * replace-menu entry, never authorable by a model (kNonAuthorableModuleTypes, docs/mixer/mixer.md#ai-authorability).
 */
class ChannelStripModule : public ModuleBase {
public:
    enum class Shape { Mono, Stereo };

    /** Raw channel carrying the right leg, in and out. See the class comment. */
    static constexpr int kRightBase = 4;
    /** Raw input channels. Fixed for the node's lifetime. */
    static constexpr int kNumInputs = kRightBase + 1;

    /** Send slots. Fixed: the parameters and the raw channels for all of them always exist; only
     *  `activeMask_` (and therefore the visible jack count) varies. */
    static constexpr int kMaxSends = 4;
    /** First raw OUTPUT channel of the send L block. 5..7 stay reserved next to the main pair. */
    static constexpr int kSendBase = 8;
    /** Raw output channels. Fixed for the node's lifetime. */
    static constexpr int kNumOutputs = kSendBase + 2 * kMaxSends;

    /** Raw output channel carrying send `slot`'s left / right leg. */
    static constexpr int sendLeftChannel(int slot) noexcept { return kSendBase + slot; }
    static constexpr int sendRightChannel(int slot) noexcept { return kSendBase + kMaxSends + slot; }

    /** Bit positions in the per-leg solo mask (see the class comment and synth::computeSoloAudibleLegs). */
    static constexpr juce::uint32 kMainLegBit = 1u;
    static constexpr juce::uint32 sendLegBit(int slot) noexcept { return 1u << (slot + 1); }

    /** The fader's floor. At (or below) this the strip is silent, not merely -60 dB. */
    static constexpr float kMinGainDb = -60.0f;
    static constexpr float kMaxGainDb = 12.0f;

    /** Gain/pan ramp length — long enough to hide a zipper on a fader drag, short enough that a
     *  mixer move feels immediate. */
    static constexpr double kSmoothingSeconds = 0.02;

    ChannelStripModule()
        : ModuleBase("Channel Strip", kNumInputs, kNumOutputs) {
        addParameter(gainParam_ = new juce::AudioParameterFloat(
                         "gain", "Gain", juce::NormalisableRange<float>(kMinGainDb, kMaxGainDb, 0.1f), 0.0f));
        addParameter(
            panParam_ = new juce::AudioParameterFloat("pan", "Pan", juce::NormalisableRange<float>(-1.0f, 1.0f), 0.0f));
        for (int slot = 0; slot < kMaxSends; ++slot) {
            const juce::String id = "send" + juce::String(slot + 1) + "Level";
            addParameter(sendLevelParams_[slot] = new juce::AudioParameterFloat(
                             id, "Send " + juce::String(slot + 1) + " Level",
                             juce::NormalisableRange<float>(kMinGainDb, kMaxGainDb, 0.1f), 0.0f));
        }
        addMuteParameter();
    }

    ~ChannelStripModule() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override {
        juce::ignoreUnused(samplesPerBlock);
        // Live in a graph from here on: the shape can no longer change (see the class comment).
        shapeLocked_.store(true, std::memory_order_relaxed);

        float targetL = 0.0f, targetR = 0.0f;
        computeTargetGains(targetL, targetR);
        smoothedGainL_.reset(sampleRate, kSmoothingSeconds);
        smoothedGainR_.reset(sampleRate, kSmoothingSeconds);
        smoothedGainL_.setCurrentAndTargetValue(targetL);
        smoothedGainR_.setCurrentAndTargetValue(targetR);
        for (int slot = 0; slot < kMaxSends; ++slot) {
            smoothedSend_[slot].reset(sampleRate, kSmoothingSeconds);
            smoothedSend_[slot].setCurrentAndTargetValue(sendTargetGain(slot));
        }
        meterLatches_[0].reset();
        meterLatches_[1].reset();
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override {
        juce::ignoreUnused(midiMessages);
        const int numSamples = buffer.getNumSamples();
        if (buffer.getNumChannels() < kNumOutputs) {
            buffer.clear();
            return;
        }

        const bool stereo = getShape() == Shape::Stereo;

        // Hidden-channel hygiene, unconditional — the reserved blocks, every send leg (a stale
        // block from the previous callback must never leak into a bus), and the right input when
        // the strip is Mono (nothing should be wired there, but the graph leaves whatever it had).
        for (int ch = 1; ch < kRightBase; ++ch)
            buffer.clear(ch, 0, numSamples);
        for (int ch = kRightBase + 1; ch < kNumOutputs; ++ch)
            buffer.clear(ch, 0, numSamples);
        // Mono: the right leg is the left input, in every branch — the output is always stereo.
        if (!stereo)
            buffer.copyFrom(kRightBase, 0, buffer, 0, 0, numSamples);

        const juce::uint32 audible = audibleLegsThisBlock();

        if (isBypassed()) {
            // Dry: no gain, no pan, so pre- and post-fader sends coincide (class comment). The solo
            // gate still applies, per leg.
            const juce::uint32 advanced = writeSendLegs(buffer, numSamples, SendPhase::All, audible);
            skipSendSmoothers(numSamples, advanced);
            applyMainLegGate(buffer, numSamples, audible);
            finishBlock(buffer, numSamples);
            return;
        }

        if (isMuted()) {
            buffer.clear();
            skipSendSmoothers(numSamples, 0);
            finishBlock(buffer, numSamples);
            return;
        }

        float targetL = 0.0f, targetR = 0.0f;
        computeTargetGains(targetL, targetR);
        smoothedGainL_.setTargetValue(targetL);
        smoothedGainR_.setTargetValue(targetR);

        juce::uint32 advanced = writeSendLegs(buffer, numSamples, SendPhase::Pre, audible);

        auto* left = buffer.getWritePointer(0);
        auto* right = buffer.getWritePointer(kRightBase);
        for (int i = 0; i < numSamples; ++i) {
            left[i] *= smoothedGainL_.getNextValue();
            right[i] *= smoothedGainR_.getNextValue();
        }

        advanced |= writeSendLegs(buffer, numSamples, SendPhase::Post, audible);
        skipSendSmoothers(numSamples, advanced);
        applyMainLegGate(buffer, numSamples, audible);
        finishBlock(buffer, numSamples);
    }

    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }

    ModuleType getModuleType() const override { return ModuleType::ChannelStrip; }
    ModulationCategory getModulationCategory() const override { return ModulationCategory::Other; }

    int getVisibleInputPortCount() const override { return getShape() == Shape::Stereo ? 2 : 1; }
    int getVisibleOutputPortCount() const override { return 2 + 2 * getActiveSendCount(); }

    juce::String getInputPortLabel(int visibleJack) const override {
        if (getShape() == Shape::Mono)
            return "In";
        return visibleJack == 1 ? "Right" : "Left";
    }
    juce::String getOutputPortLabel(int visibleJack) const override {
        if (visibleJack < 2)
            return visibleJack == 1 ? "Right" : "Left";
        const int slot = slotForVisibleJack(visibleJack);
        if (slot < 0)
            return ModuleBase::getOutputPortLabel(visibleJack);
        // Named by SLOT, not by visible position: removing a middle send renumbers the jacks but
        // must not renumber what a row and its sendNLevel parameter call themselves.
        return "Send " + juce::String(slot + 1) + ((visibleJack % 2) == 0 ? " L" : " R");
    }

    LogicalPort mapInputChannel(int rawChannel) const override {
        if (rawChannel == 0)
            return audioJack(0);
        if (rawChannel == kRightBase && getShape() == Shape::Stereo)
            return audioJack(1);
        return {}; // reserved / hidden: role Other, not a group head
    }
    LogicalPort mapOutputChannel(int rawChannel) const override {
        if (rawChannel == 0)
            return audioJack(0);
        if (rawChannel == kRightBase)
            return audioJack(1);
        for (int slot = 0; slot < kMaxSends; ++slot) {
            if (!isSendActive(slot))
                continue;
            if (rawChannel == sendLeftChannel(slot))
                return audioJack(visibleJackForSlot(slot));
            if (rawChannel == sendRightChannel(slot))
                return audioJack(visibleJackForSlot(slot) + 1);
        }
        return {};
    }

    int rightAudioLegChannel() const override { return kRightBase; }

    // ---- Shape (message thread) ----

    /** Sets the input shape. Called ONCE by the channel-creation flow, right after construction
     *  and before the node is added to a running graph. Returns false — and changes nothing — when
     *  the shape is already locked to a DIFFERENT value; re-asserting the current shape is a no-op
     *  that succeeds. */
    bool setShape(Shape shape) {
        if (shapeLocked_.load(std::memory_order_relaxed))
            return shape_.load(std::memory_order_relaxed) == shape;
        shape_.store(shape, std::memory_order_relaxed);
        shapeLocked_.store(true, std::memory_order_relaxed);
        return true;
    }
    Shape getShape() const { return shape_.load(std::memory_order_relaxed); }

    // ---- Sends (message thread writes, audio thread reads) ----
    //
    // Pure slot bookkeeping: these never touch the graph. Wiring a slot's raw channels to a bus (and
    // clearing that cable again on removal) is synth::MixerSends' business — the strip only knows
    // which slots EXIST, so that their jacks exist for a cable to land on.

    bool isSendActive(int slot) const noexcept {
        return slot >= 0 && slot < kMaxSends && (activeMask_.load(std::memory_order_relaxed) & (1u << slot)) != 0;
    }
    bool isSendPreFader(int slot) const noexcept {
        return slot >= 0 && slot < kMaxSends && (preMask_.load(std::memory_order_relaxed) & (1u << slot)) != 0;
    }
    int getActiveSendCount() const noexcept {
        const auto mask = activeMask_.load(std::memory_order_relaxed);
        int count = 0;
        for (int slot = 0; slot < kMaxSends; ++slot)
            if ((mask & (1u << slot)) != 0)
                ++count;
        return count;
    }
    juce::uint32 getActiveSendMask() const noexcept { return activeMask_.load(std::memory_order_relaxed); }

    /** DISPLAY ONLY (docs/mixer/sends-and-buses.md): this strip was created as a group/send bus rather than as
     *  a track's channel, so the mixer gives its column the BUS badge and a feeding-strips source
     *  line instead of a track chip. Written by the "Add bus" flow and by buildMakeChannel's
     *  merge-point buses; persisted in the trusted extra state. Nothing about routing or audio
     *  depends on it -- a bus IS an ordinary Channel Strip (that is the whole point of D1) -- so a
     *  patch saved before the flag existed still classifies correctly from its topology. */
    void setIsBus(bool isBus) noexcept { isBus_.store(isBus, std::memory_order_relaxed); }
    bool isBus() const noexcept { return isBus_.load(std::memory_order_relaxed); }

    /** Activates the lowest free slot and returns it, or -1 when all kMaxSends are in use. A new
     *  send is post-fader at unity (see the class comment's "audible rather than looking broken"). */
    int addSend() {
        for (int slot = 0; slot < kMaxSends; ++slot) {
            if (isSendActive(slot))
                continue;
            setSendActive(slot, true);
            setSendPreFader(slot, false);
            return slot;
        }
        return -1;
    }

    /** Clears `slot`'s bit. Raw channels stay pinned to the slot, so higher slots never move. */
    void setSendActive(int slot, bool active) {
        if (slot < 0 || slot >= kMaxSends)
            return;
        const juce::uint32 bit = 1u << slot;
        auto mask = activeMask_.load(std::memory_order_relaxed);
        activeMask_.store(active ? (mask | bit) : (mask & ~bit), std::memory_order_relaxed);
    }
    void setSendPreFader(int slot, bool pre) {
        if (slot < 0 || slot >= kMaxSends)
            return;
        const juce::uint32 bit = 1u << slot;
        auto mask = preMask_.load(std::memory_order_relaxed);
        preMask_.store(pre ? (mask | bit) : (mask & ~bit), std::memory_order_relaxed);
    }

    /** This slot's level parameter — the mixer row's knob attaches to it directly, so send level is
     *  host-visible and automatable for free. Null only for an out-of-range slot. */
    juce::AudioParameterFloat* getSendLevelParameter(int slot) const noexcept {
        return slot >= 0 && slot < kMaxSends ? sendLevelParams_[slot] : nullptr;
    }
    static juce::String getSendLevelParameterId(int slot) { return "send" + juce::String(slot + 1) + "Level"; }

    // ---- Solo (message thread writes, audio thread reads) ----

    /** This strip's own solo flag. Callers that change it must let the engine recount
     *  (AudioEngine::setChannelStripSoloed does both), or the "is anything soloed?" gate goes stale. */
    void setSoloed(bool soloed) noexcept { soloed_.store(soloed, std::memory_order_relaxed); }
    bool isSoloed() const noexcept { return soloed_.load(std::memory_order_relaxed); }

    /** Which of this strip's legs stay audible while the mixer solo gate is active — bit 0 the main
     *  pair, bit 1+k send slot k. Published by AudioEngine::refreshSoloGate from
     *  synth::computeSoloAudibleLegs; read once per block. Ignored entirely while the gate is open
     *  or this strip is itself soloed. See the class comment for why 0 is the right default. */
    void setSoloAudibleMask(juce::uint32 mask) noexcept { soloAudibleMask_.store(mask, std::memory_order_relaxed); }
    juce::uint32 getSoloAudibleMask() const noexcept { return soloAudibleMask_.load(std::memory_order_relaxed); }
    /** refreshSoloGate's open-before-close first pass: never makes a leg less audible. */
    void orSoloAudibleMask(juce::uint32 mask) noexcept {
        soloAudibleMask_.store(soloAudibleMask_.load(std::memory_order_relaxed) | mask, std::memory_order_relaxed);
    }

    // ---- Meter (audio thread writes, any thread reads) ----

    /** FRO146: the peak latched since `reader`'s own last call, for one output leg (0 = Left,
     *  1 = Right), post gain/pan/mute/solo. Consuming (read-and-reset) but only of `reader`'s own
     *  slot -- the mixer column and a track header's channel chip poll independently and must
     *  never steal each other's peaks. See Source/Mixer/PeakMeterLatch.h. */
    float takeMeterPeak(synth::MeterReader reader, int leg) noexcept {
        return meterLatches_[legIndex(leg)].takePeak(reader);
    }

    // ---- Stem export tap (message thread arms/disarms; audio thread reads the pointer and writes
    // through it every block). See the class comment. `buffer` must outlive every processBlock call
    // made while it is armed, and must hold >= the render's block size in 2 channels; pass nullptr
    // to disarm. Never call this while the graph this strip belongs to is rendering.
    void setStemTapBuffer(juce::AudioBuffer<float>* buffer) noexcept {
        stemTap_.store(buffer, std::memory_order_release);
    }

    // Test seam: true while a tap is armed. Lets a test prove a session disarms every tap on the
    // way out (cancel, failure, or normal completion) without needing a dangling pointer into
    // memory the session already freed.
    bool isStemTapArmedForTest() const noexcept { return stemTap_.load(std::memory_order_acquire) != nullptr; }

    // ---- Non-parameter state. TRUSTED-PATH ONLY — AIStateMapper never calls setExtraState for
    // model output, and this type is additionally refused outright on the untrusted path
    // (kNonAuthorableModuleTypes). ----

    juce::var getExtraState() const override {
        auto* obj = new juce::DynamicObject();
        obj->setProperty("shape", getShape() == Shape::Mono ? "mono" : "stereo");
        obj->setProperty("solo", isSoloed());
        obj->setProperty("isBus", isBus());
        juce::Array<juce::var> sends;
        for (int slot = 0; slot < kMaxSends; ++slot) {
            if (!isSendActive(slot))
                continue;
            auto* entry = new juce::DynamicObject();
            entry->setProperty("slot", slot);
            entry->setProperty("pre", isSendPreFader(slot));
            sends.add(juce::var(entry));
        }
        obj->setProperty("sends", sends);
        return juce::var(obj);
    }

    void setExtraState(const juce::var& state) override {
        if (auto* obj = state.getDynamicObject()) {
            if (obj->hasProperty("shape"))
                setShape(obj->getProperty("shape").toString() == "mono" ? Shape::Mono : Shape::Stereo);
            if (obj->hasProperty("solo"))
                setSoloed(static_cast<bool>(obj->getProperty("solo")));
            if (obj->hasProperty("isBus"))
                setIsBus(static_cast<bool>(obj->getProperty("isBus")));
            if (obj->hasProperty("sends"))
                readSendsState(obj->getProperty("sends"));
        }
    }

private:
    enum class SendPhase { Pre, Post, All };

    static LogicalPort audioJack(int visibleJack) noexcept {
        LogicalPort p;
        p.visibleJackIndex = visibleJack;
        p.role = PortRole::Audio;
        p.isPolyGroupHead = true;
        p.polyVoiceSpan = 1;
        return p;
    }

    /** First visible output jack of `slot`'s pair: the two main jacks, then two per ACTIVE slot
     *  below this one. Only meaningful for an active slot. */
    int visibleJackForSlot(int slot) const noexcept {
        int position = 0;
        for (int below = 0; below < slot; ++below)
            if (isSendActive(below))
                ++position;
        return 2 + 2 * position;
    }

    /** Inverse of visibleJackForSlot for the L jack of a pair; -1 when `visibleJack` names no
     *  active slot. */
    int slotForVisibleJack(int visibleJack) const noexcept {
        for (int slot = 0; slot < kMaxSends; ++slot)
            if (isSendActive(slot) &&
                (visibleJack == visibleJackForSlot(slot) || visibleJack == visibleJackForSlot(slot) + 1))
                return slot;
        return -1;
    }

    void readSendsState(const juce::var& sends) {
        activeMask_.store(0, std::memory_order_relaxed);
        preMask_.store(0, std::memory_order_relaxed);
        if (const auto* array = sends.getArray()) {
            for (const auto& entry : *array) {
                auto* obj = entry.getDynamicObject();
                if (obj == nullptr)
                    continue;
                const int slot = static_cast<int>(obj->getProperty("slot"));
                if (slot < 0 || slot >= kMaxSends)
                    continue;
                setSendActive(slot, true);
                setSendPreFader(slot, static_cast<bool>(obj->getProperty("pre")));
            }
        }
    }

    void computeTargetGains(float& targetL, float& targetR) const {
        const float gain = juce::Decibels::decibelsToGain(gainParam_->get(), kMinGainDb);
        float panL = 1.0f, panR = 1.0f;
        panGains(panParam_->get(), panL, panR);
        targetL = gain * panL;
        targetR = gain * panR;
    }

    float sendTargetGain(int slot) const {
        return juce::Decibels::decibelsToGain(sendLevelParams_[slot]->get(), kMinGainDb);
    }

    /** Which legs may be written this block. ~0u whenever the mixer solo gate is open, or this
     *  strip is the soloed one; otherwise the published per-leg mask. See the class comment. */
    juce::uint32 audibleLegsThisBlock() const {
        auto* transport = dynamic_cast<synth::TransportService*>(getPlayHead());
        if (transport == nullptr || !transport->isMixerSoloActiveForBlock())
            return ~0u;
        if (isSoloed())
            return ~0u;
        return soloAudibleMask_.load(std::memory_order_relaxed);
    }

    /** Writes every ACTIVE slot in `phase` from the current ch0/kRightBase content, scaled by that
     *  slot's own smoothed level; a slot whose solo bit is closed is left at the silence the
     *  hygiene pass already wrote. Returns the mask of slots whose smoother this call advanced, so
     *  the caller can skip the rest exactly once per block. */
    juce::uint32 writeSendLegs(juce::AudioBuffer<float>& buffer, int numSamples, SendPhase phase,
                               juce::uint32 audible) noexcept {
        const juce::uint32 active = activeMask_.load(std::memory_order_relaxed);
        const juce::uint32 pre = preMask_.load(std::memory_order_relaxed);
        juce::uint32 advanced = 0;

        for (int slot = 0; slot < kMaxSends; ++slot) {
            const juce::uint32 bit = 1u << slot;
            if ((active & bit) == 0)
                continue;
            if (phase != SendPhase::All && (((pre & bit) != 0) != (phase == SendPhase::Pre)))
                continue;

            advanced |= bit;
            auto& smoothed = smoothedSend_[slot];
            smoothed.setTargetValue(sendTargetGain(slot));
            if ((audible & sendLegBit(slot)) == 0) {
                smoothed.skip(numSamples);
                continue; // already silent from the hygiene pass
            }

            const auto* sourceL = buffer.getReadPointer(0);
            const auto* sourceR = buffer.getReadPointer(kRightBase);
            auto* destL = buffer.getWritePointer(sendLeftChannel(slot));
            auto* destR = buffer.getWritePointer(sendRightChannel(slot));
            for (int i = 0; i < numSamples; ++i) {
                const float level = smoothed.getNextValue();
                destL[i] = sourceL[i] * level;
                destR[i] = sourceR[i] * level;
            }
        }
        return advanced;
    }

    /** Advances every slot NOT in `advanced`, so an inactive or gated send's ramp stays where the
     *  level parameter actually is — the same discipline the main gains follow. */
    void skipSendSmoothers(int numSamples, juce::uint32 advanced) noexcept {
        for (int slot = 0; slot < kMaxSends; ++slot)
            if ((advanced & (1u << slot)) == 0)
                smoothedSend_[slot].skip(numSamples);
    }

    void applyMainLegGate(juce::AudioBuffer<float>& buffer, int numSamples, juce::uint32 audible) noexcept {
        if ((audible & kMainLegBit) != 0)
            return;
        buffer.clear(0, 0, numSamples);
        buffer.clear(kRightBase, 0, numSamples);
    }

    // The one place every processBlock exit path converges: updates the meter and, if a stem tap is
    // armed, copies this block's FINAL output into it. `buffer` at this point is exactly what the
    // strip hands to Master, in every branch (dry-bypassed, muted, solo-gated silent, or normal) —
    // see the class comment. Main legs only: a send leg is the bus's stem, never this strip's.
    void finishBlock(const juce::AudioBuffer<float>& buffer, int numSamples) noexcept {
        meterLatches_[0].storeBlockPeak(buffer.getMagnitude(0, 0, numSamples));
        meterLatches_[1].storeBlockPeak(buffer.getMagnitude(kRightBase, 0, numSamples));

        if (auto* tap = stemTap_.load(std::memory_order_acquire)) {
            // Defensive, not expected: a caller-sized-wrong tap must never be overrun. See the class
            // comment for the sizing contract StemSession upholds.
            if (tap->getNumChannels() >= 2 && tap->getNumSamples() >= numSamples) {
                tap->copyFrom(0, 0, buffer, 0, 0, numSamples);
                tap->copyFrom(1, 0, buffer, kRightBase, 0, numSamples);
            }
        }
    }

    juce::AudioParameterFloat* gainParam_ = nullptr;
    juce::AudioParameterFloat* panParam_ = nullptr;
    juce::AudioParameterFloat* sendLevelParams_[kMaxSends] = {};

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedGainL_{1.0f};
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedGainR_{1.0f};
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedSend_[kMaxSends];

    // Written on the message thread (setShape / setExtraState, before the node is live), read every
    // block on the audio thread. Relaxed: nothing orders against it.
    std::atomic<Shape> shape_{Shape::Stereo};
    std::atomic<bool> shapeLocked_{false};
    std::atomic<bool> soloed_{false};
    std::atomic<bool> isBus_{false};

    // Send slot bookkeeping. Message-thread writes (the send flows, setExtraState), audio-thread
    // reads — one relaxed load each per block, so a strip's own slots are always self-consistent.
    std::atomic<juce::uint32> activeMask_{0};
    std::atomic<juce::uint32> preMask_{0};
    std::atomic<juce::uint32> soloAudibleMask_{0};

    static int legIndex(int leg) noexcept { return leg == 1 ? 1 : 0; }

    std::array<synth::PeakMeterLatch, 2> meterLatches_;

    // Non-owning; null outside a stem export. See setStemTapBuffer() and the class comment.
    std::atomic<juce::AudioBuffer<float>*> stemTap_{nullptr};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChannelStripModule)
};
