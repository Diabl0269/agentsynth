#pragma once

#include "Envelope/EnvelopeGenerator.h"
#include "Envelope/EnvelopeTempoSync.h"
#include "ModuleBase.h"
#include "SchmittTrigger.h"
#include "ThresholdMeterSource.h"
#include <algorithm>
#include <atomic>
#include <bitset>
#include <cmath>
#include <vector>

class ADSRModule
    : public ModuleBase
    , public ThresholdMeterSource {
public:
    ADSRModule(const juce::String& name = "ADSR")
        : ModuleBase(name, 14, 14) // 8 gate CV per voice + shared Threshold/Attack/Hold/Decay/Sustain/Release
                                   // CV (ch8-13, FRO285); 8 env + silent ch8-13. Outputs match inputs so the
                                   // highest CV channel read (13) never aliases a live graph buffer -- see
                                   // docs/modules/poly-channel-layout.md.
    {
        // Times move to a LINEAR [0, 5] range; the retired minimum-time clamps (2 ms attack /
        // 5 ms release) are gone from the PARAMETER on purpose -- 0 ms is a real, reachable,
        // displayable value, not a bug, and nothing here silently raises it. `EnvelopeGenerator`
        // still floors the stage's *effective* time internally to a fixed, much smaller
        // click-free minimum (0.1 ms attack, 1 ms decay/release; see its `kMinAttackSeconds` /
        // `kMinRampSeconds`) -- a one-sample full-scale level jump is audible regardless of
        // whether the user asked for 0 ms explicitly or an automation lane swept down to it, so
        // "0 ms" reads as "as fast as is click-free", not as a literal single-sample cliff. The
        // knob feel at the new 1 ms attack default lives on the slider itself
        // (ModuleComponent.cpp's ADSR special case, applyAdsrTimeSliderSkew), never on the
        // parameter's own range -- a skewed NormalisableRange here would badly worsen
        // AIStateMapper's pre-existing untrusted in-[0,1] rescale misfire. See
        // docs/modules/modules.md#adsr-envelope-module for the full rationale and for why 5.0 stays the ceiling.
        //
        // FRO112: every float param below also carries readout Attributes (ms/s for the four
        // stage times, dB for sustain, plain for the three curve amounts) so the envelope card's
        // knobs and any host's generic automation UI show clean text ("1.0 ms", "-6.0 dB")
        // instead of the raw linear value ("0.0010000000...") -- see adsrTimeAttributes() /
        // adsrSustainAttributes() / adsrCurveAttributes() below. This is display-only: the
        // NormalisableRange stays exactly as it was (linear, unskewed) for patch compatibility
        // and the AIStateMapper rescale heuristic.
        addParameter(attackParam = new juce::AudioParameterFloat(
                         "attack", "Attack", juce::NormalisableRange<float>(0.0f, 5.0f), 0.001f, adsrTimeAttributes()));
        addParameter(decayParam = new juce::AudioParameterFloat(
                         "decay", "Decay", juce::NormalisableRange<float>(0.0f, 5.0f), 1.0f, adsrTimeAttributes()));
        addParameter(sustainParam =
                         new juce::AudioParameterFloat("sustain", "Sustain", juce::NormalisableRange<float>(0.0f, 1.0f),
                                                       1.0f, adsrSustainAttributes()));
        addParameter(releaseParam =
                         new juce::AudioParameterFloat("release", "Release", juce::NormalisableRange<float>(0.0f, 5.0f),
                                                       0.015f, adsrTimeAttributes()));
        addParameter(holdParam = new juce::AudioParameterFloat(
                         "hold", "Hold", juce::NormalisableRange<float>(0.0f, 5.0f), 0.0f, adsrTimeAttributes()));
        addParameter(attackCurveParam = new juce::AudioParameterFloat("attackCurve", "Attack Curve",
                                                                      juce::NormalisableRange<float>(-1.0f, 1.0f),
                                                                      -0.3f, adsrCurveAttributes()));
        addParameter(decayCurveParam = new juce::AudioParameterFloat("decayCurve", "Decay Curve",
                                                                     juce::NormalisableRange<float>(-1.0f, 1.0f), 0.65f,
                                                                     adsrCurveAttributes()));
        addParameter(releaseCurveParam = new juce::AudioParameterFloat("releaseCurve", "Release Curve",
                                                                       juce::NormalisableRange<float>(-1.0f, 1.0f),
                                                                       0.65f, adsrCurveAttributes()));
        // Tempo sync (FRO113): BPM mode is REAL sync, not a display-only snap -- each timed stage
        // gets its own note-division choice, sharing `envelopeNoteDivisions()` (the same six
        // entries/order as LFOModule's rateSync). `tempoSync` off (default) leaves the ms params
        // above in sole control, unchanged; the four *Div params still exist and round-trip in
        // every patch either way, so flipping the toggle later never loses a prior sync setting.
        // Defaults are the closest available division to each ms default at 120 BPM -- decay's
        // 1.0 s default lands on "1/2" exactly (2 beats @ 120 BPM); attack/hold/release all want
        // something far shorter than the coarsest division below "1/32" gets them (62.5 ms vs.
        // 1/0/15 ms), which is an accepted tradeoff of sync's coarser resolution, not a bug.
        addParameter(tempoSyncParam = new juce::AudioParameterBool("tempoSync", "Tempo Sync", false));
        addParameter(attackDivParam =
                         new juce::AudioParameterChoice("attackDiv", "Attack Div", synth::envelopeNoteDivisions(), 5));
        addParameter(holdDivParam =
                         new juce::AudioParameterChoice("holdDiv", "Hold Div", synth::envelopeNoteDivisions(), 5));
        addParameter(decayDivParam =
                         new juce::AudioParameterChoice("decayDiv", "Decay Div", synth::envelopeNoteDivisions(), 1));
        addParameter(releaseDivParam = new juce::AudioParameterChoice("releaseDiv", "Release Div",
                                                                      synth::envelopeNoteDivisions(), 5));
        // `gateThreshold`, not `threshold` / `trigThreshold`: Compressor owns `threshold` as dB,
        // Sample & Hold / Comparator own `trigThreshold` as bipolar CV. ADSR gates are unipolar.
        addParameter(thresholdParam = new juce::AudioParameterFloat("gateThreshold", "Threshold", 0.0f, 1.0f, 0.5f));
        addParameter(polyParam = new juce::AudioParameterBool("poly", "Poly", false));
        addMuteParameter();
        enableVisualBuffer(true);
    }

    void prepareToPlay(double sampleRate, int samplesPerBlock) override {
        juce::ignoreUnused(samplesPerBlock);
        for (int v = 0; v < MAX_VOICES; ++v) {
            envelopes[v].setSampleRate(sampleRate);
            envelopes[v].reset();
            gateTriggers[v].reset();
            previousActive[v] = false;
        }
        heldNotes.reset();
        lastTriggeredVoice = 0;
        resetMeters();
        effectiveThreshold.store(thresholdParam->get(), std::memory_order_relaxed);
        playheadStage.store(synth::EnvelopeStage::Idle, std::memory_order_relaxed);
        playheadProgress.store(0.0f, std::memory_order_relaxed);
        playheadLevel.store(0.0f, std::memory_order_relaxed);
        lastSeenBpm.store(120.0, std::memory_order_relaxed);
        // Sustain is the one envelope parameter that is a LEVEL, not a stage time:
        // EnvelopeGenerator reads it fresh every sample (as Decay's live target and as the flat
        // Sustain output), so a per-sample smoother lets automation retarget an in-flight decay
        // or a held note smoothly instead of stepping. Attack/Hold/Decay/Release are stage
        // *times* and are deliberately left unsmoothed -- EnvelopeGenerator turns a time change
        // mid-ramp into a slope change, never a level jump, entirely on its own.
        smoothedSustain.reset(sampleRate, 0.02);
        smoothedSustain.setCurrentAndTargetValue(sustainParam->get());
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override {
        if (buffer.getNumSamples() == 0 || buffer.getNumChannels() == 0)
            return;

        // Pure source / CV generator: there is no dry audio path, so both branches clear.
        // Kept as two conditions so a fused `isBypassed() || isMuted()` cannot sneak back in.
        if (isBypassed()) {
            buffer.clear();
            resetMeters();
            return;
        }
        if (isMuted()) {
            buffer.clear();
            resetMeters();
            return;
        }

        const int numChannels = buffer.getNumChannels();
        const int numSamples = buffer.getNumSamples();

        // Sustain is read fresh every sample directly off `smoothedSustain` in each per-sample
        // loop below (never pre-computed into a buffer sized off `numSamples`): a host is free
        // to hand a block larger than the `samplesPerBlock` given to `prepareToPlay`, and the
        // audio callback must never allocate (root CLAUDE.md), so there is deliberately no
        // scratch vector here to resize.
        // Sustain CV (FRO285): a LEVEL, not a stage time, so tempo sync -- which only recomputes
        // the four *time* params below -- never gates it; it always applies. Read once per block
        // like every other parameter-CV jack added under the normalised-CV convention
        // (docs/modules/modulation.md#cv-in-normalised-units), then fed into the same per-sample
        // smoother the base parameter already used.
        smoothedSustain.setTargetValue(
            modulateNormalised(*sustainParam, *sustainParam, blockCV(buffer, kSustainChannel)));

        const bool poly = *polyParam;
        const float baseThreshold = thresholdParam->get();
        const float* thresholdCV = numChannels > kThresholdChannel ? buffer.getReadPointer(kThresholdChannel) : nullptr;

        const StageTimes times = resolveStageTimes(buffer);
        const float attack = times.attack;
        const float hold = times.hold;
        const float decay = times.decay;
        const float release = times.release;
        const float attackCurve = *attackCurveParam;
        const float decayCurve = *decayCurveParam;
        const float releaseCurve = *releaseCurveParam;

        float meterPeak = 0.0f;
        float lastThreshold = baseThreshold;
        int firedThisBlock = 0;

        if (!poly) {
            const float* gateIn = buffer.getReadPointer(0);
            float* envOut = buffer.getWritePointer(0);

            for (int smp = 0; smp < numSamples; ++smp) {
                bool midiNoteOnThisSample = false;
                for (const auto metadata : midiMessages) {
                    if (metadata.samplePosition != smp)
                        continue;
                    const auto message = metadata.getMessage();
                    if (message.isNoteOn()) {
                        heldNotes.set(static_cast<size_t>(message.getNoteNumber()));
                        // The note-on EVENT drives the retrigger, not an edge in a held/not-held
                        // flag -- a note-off + note-on landing on the same sample (a gapless
                        // mono legato transition) nets out to "still held" on the flag, but must
                        // still re-articulate (FRO110).
                        envelopes[0].noteOn();
                        lastTriggeredVoice = 0;
                        ++firedThisBlock;
                        midiNoteOnThisSample = true;
                    } else if (message.isNoteOff()) {
                        heldNotes.reset(static_cast<size_t>(message.getNoteNumber()));
                    } else if (message.isAllNotesOff() || message.isAllSoundOff()) {
                        heldNotes.reset();
                    }
                }

                const float gateSample = gateIn[smp];
                if (std::abs(gateSample) > std::abs(meterPeak))
                    meterPeak = gateSample;

                float threshold = baseThreshold;
                if (thresholdCV != nullptr)
                    threshold = juce::jlimit(0.0f, 1.0f, threshold + thresholdCV[smp]);
                lastThreshold = threshold;

                gateTriggers[0].process(gateSample, threshold);
                // Release only once every held note is gone AND the Gate CV is low -- the OR
                // with the Schmitt trigger's own held state is unchanged from before.
                const bool active = heldNotes.any() || gateTriggers[0].high;
                if (active && !previousActive[0] && !midiNoteOnThisSample) {
                    envelopes[0].noteOn();
                    lastTriggeredVoice = 0;
                    ++firedThisBlock;
                } else if (!active && previousActive[0]) {
                    envelopes[0].noteOff();
                }
                previousActive[0] = active;

                envOut[smp] =
                    envelopes[0].getNextSample(makeParameters(attack, hold, decay, smoothedSustain.getNextValue(),
                                                              release, attackCurve, decayCurve, releaseCurve));
            }

            for (const auto metadata : midiMessages) {
                if (metadata.samplePosition < numSamples)
                    continue;
                const auto message = metadata.getMessage();
                if (message.isNoteOn())
                    heldNotes.set(static_cast<size_t>(message.getNoteNumber()));
                else if (message.isNoteOff())
                    heldNotes.reset(static_cast<size_t>(message.getNoteNumber()));
                else if (message.isAllNotesOff() || message.isAllSoundOff())
                    heldNotes.reset();
            }

            for (int ch = 1; ch < numChannels; ++ch)
                buffer.clear(ch, 0, numSamples);

            if (auto* vb = getVisualBuffer())
                for (int smp = 0; smp < numSamples; ++smp)
                    vb->pushSample(envOut[smp]);
        } else {
            float* voiceData[MAX_VOICES] = {};
            const int voices = std::min(MAX_VOICES, numChannels);
            for (int v = 0; v < voices; ++v)
                voiceData[v] = buffer.getWritePointer(v);

            for (int smp = 0; smp < numSamples; ++smp) {
                float threshold = baseThreshold;
                if (thresholdCV != nullptr)
                    threshold = juce::jlimit(0.0f, 1.0f, threshold + thresholdCV[smp]);
                lastThreshold = threshold;

                const synth::EnvelopeParameters ep = makeParameters(attack, hold, decay, smoothedSustain.getNextValue(),
                                                                    release, attackCurve, decayCurve, releaseCurve);

                for (int v = 0; v < voices; ++v) {
                    const float gateSample = voiceData[v][smp];
                    if (v == 0 && std::abs(gateSample) > std::abs(meterPeak))
                        meterPeak = gateSample;

                    const auto edge = gateTriggers[v].process(gateSample, threshold);
                    if (edge == SchmittTrigger::Edge::Rising) {
                        envelopes[v].noteOn();
                        lastTriggeredVoice = v;
                        if (v == 0)
                            ++firedThisBlock;
                    } else if (edge == SchmittTrigger::Edge::Falling) {
                        envelopes[v].noteOff();
                    }
                    voiceData[v][smp] = envelopes[v].getNextSample(ep);
                }
            }

            for (int ch = voices; ch < numChannels; ++ch)
                buffer.clear(ch, 0, numSamples);

            if (auto* vb = getVisualBuffer())
                for (int smp = 0; smp < numSamples; ++smp)
                    vb->pushSample(buffer.getSample(0, smp));
        }

        meterLevel.store(meterPeak, std::memory_order_relaxed);
        effectiveThreshold.store(lastThreshold, std::memory_order_relaxed);
        overThreshold.store(poly ? gateTriggers[0].high : previousActive[0], std::memory_order_relaxed);
        if (firedThisBlock > 0)
            triggerCount.fetch_add(firedThisBlock, std::memory_order_relaxed);

        // Lock-free UI playhead for the most recently (re)triggered voice, written once per
        // block -- not per sample -- for a future graph-editor overlay.
        const int voiceForPlayhead = juce::jlimit(0, MAX_VOICES - 1, lastTriggeredVoice);
        playheadStage.store(envelopes[voiceForPlayhead].getStage(), std::memory_order_relaxed);
        playheadProgress.store(envelopes[voiceForPlayhead].getProgress(), std::memory_order_relaxed);
        playheadLevel.store(envelopes[voiceForPlayhead].getLevel(), std::memory_order_relaxed);
    }

    // processBlock consumes note-on/off to drive heldNotes (a MIDI fallback for the Gate
    // input) but never writes to the MIDI buffer.
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }

    ModulationCategory getModulationCategory() const override { return ModulationCategory::Envelope; }
    // An envelope rises from 0, it never swings negative -- the depth band is [base, base+amount].
    bool isModSourceBipolar() const override { return false; }
    juce::String getInputPortLabel(int i) const override {
        switch (i) {
        case 0:
            return "Gate";
        case 1:
            return "Threshold";
        case 2:
            return "Attack";
        case 3:
            return "Hold";
        case 4:
            return "Decay";
        case 5:
            return "Sustain";
        case 6:
            return "Release";
        default:
            return ModuleBase::getInputPortLabel(i);
        }
    }
    juce::String getOutputPortLabel(int) const override { return "Env"; }
    int getVisibleInputPortCount() const override {
        return 7;
    } // Gate, Threshold, Attack, Hold, Decay, Sustain, Release
    int getVisibleOutputPortCount() const override { return 1; }
    ModuleType getModuleType() const override { return ModuleType::ADSR; }

    std::vector<ModulationTarget> getModulationTargets() const override {
        return {{"Threshold", kThresholdChannel},        {"Attack", kAttackChannel, "attack"},
                {"Hold", kHoldChannel, "hold"},          {"Decay", kDecayChannel, "decay"},
                {"Sustain", kSustainChannel, "sustain"}, {"Release", kReleaseChannel, "release"}};
    }

    LogicalPort mapInputChannel(int raw) const override {
        LogicalPort p;
        if (polyParam->get()) {
            if (raw >= 0 && raw <= 7) {
                p.visibleJackIndex = 0;
                p.role = PortRole::Gate;
                p.isPolyGroupHead = (raw == 0);
                p.polyVoiceSpan = (raw == 0) ? 8 : 1;
                return p;
            }
        } else if (raw == 0) {
            p.visibleJackIndex = 0;
            p.role = PortRole::Gate;
            p.isPolyGroupHead = true;
            p.polyVoiceSpan = 1;
            return p;
        }
        // Threshold + the five stage-time/level CV jacks (FRO285): shared across every voice,
        // exactly like Threshold -- each is its own mono jack, never a poly fan, so a signal on
        // ch9-13 alone is never mistaken for a poly gate head (mapInputChannel is what decides
        // that, not the raw channel range alone -- see docs/modules/poly-channel-layout.md).
        struct SharedCvJack {
            int channel;
            int jack;
        };
        static constexpr SharedCvJack kSharedCvJacks[] = {
            {kThresholdChannel, 1}, {kAttackChannel, 2},  {kHoldChannel, 3},
            {kDecayChannel, 4},     {kSustainChannel, 5}, {kReleaseChannel, 6},
        };
        for (const auto& jack : kSharedCvJacks) {
            if (raw == jack.channel) {
                p.visibleJackIndex = jack.jack;
                p.role = PortRole::ModCV;
                p.isPolyGroupHead = true;
                p.polyVoiceSpan = 1;
                return p;
            }
        }
        return ModuleBase::mapInputChannel(raw);
    }

    LogicalPort mapOutputChannel(int raw) const override {
        LogicalPort p;
        if (polyParam->get()) {
            if (raw >= 0 && raw <= 7) {
                p.visibleJackIndex = 0;
                p.role = PortRole::ModCV;
                p.isPolyGroupHead = (raw == 0);
                p.polyVoiceSpan = (raw == 0) ? 8 : 1;
                return p;
            }
        } else if (raw == 0) {
            p.visibleJackIndex = 0;
            p.role = PortRole::ModCV;
            p.isPolyGroupHead = true;
            p.polyVoiceSpan = 1;
            return p;
        }
        return ModuleBase::mapOutputChannel(raw);
    }

    float getMeterLevel() const override { return meterLevel.load(std::memory_order_relaxed); }
    float getEffectiveThreshold() const override { return effectiveThreshold.load(std::memory_order_relaxed); }
    bool isOverThreshold() const override { return overThreshold.load(std::memory_order_relaxed); }
    int getTriggerCount() const override { return triggerCount.load(std::memory_order_relaxed); }
    ThresholdScale getThresholdScale() const override { return ThresholdScale::Unipolar; }
    juce::String getThresholdParamID() const override { return "gateThreshold"; }
    juce::String getMeterIdleLabel() const override { return "no gate"; }

    static constexpr float getTriggerHysteresis() { return SchmittTrigger::kHysteresis; }

    // Lock-free UI playhead accessors (FRO110): the stage/progress/level of the most recently
    // (re)triggered voice, refreshed once per block. Not yet consumed by any UI.
    synth::EnvelopeStage getPlayheadStage() const noexcept { return playheadStage.load(std::memory_order_relaxed); }
    float getPlayheadProgress() const noexcept { return playheadProgress.load(std::memory_order_relaxed); }
    float getPlayheadLevel() const noexcept { return playheadLevel.load(std::memory_order_relaxed); }

    // The tempo (BPM) resolveStageTimes last read off the playhead, refreshed every block whether
    // or not tempoSync is on; default 120 before the first processBlock. FRO118: the envelope
    // card's BPM-mode graph/pickers read this rather than touching getPlayHead() themselves.
    double getLastSeenBpm() const noexcept { return lastSeenBpm.load(std::memory_order_relaxed); }

private:
    struct StageTimes {
        float attack, hold, decay, release;
    };

    // Per-block stage times: the ms knobs (plus their CV), or the tempo-synced divisions.
    StageTimes resolveStageTimes(const juce::AudioBuffer<float>& buffer) {
        // Tempo sync (FRO113): recomputed once per block, same cadence LFOModule uses for its
        // own sync mode -- a live tempo change is picked up within one block, and because
        // EnvelopeGenerator turns a stage-time change mid-ramp into a slope change (never a
        // level jump), flipping tempoSync itself mid-note is exactly as click-free as automating
        // one of the ms params already was.
        float attack = *attackParam;
        float hold = *holdParam;
        float decay = *decayParam;
        float release = *releaseParam;
        double bpm = 120.0;
        if (auto* ph = getPlayHead()) {
            if (auto pos = ph->getPosition()) {
                if (pos->getBpm().hasValue())
                    bpm = *pos->getBpm();
            }
        }
        // Published for the UI (FRO118's BPM-mode graph/pickers): read via getLastSeenBpm(), never
        // getPlayHead() directly -- TransportService's block-only lifetime (Source/CLAUDE.md) makes
        // it unsafe to hold or call from the message thread.
        lastSeenBpm.store(bpm, std::memory_order_relaxed);
        if (*tempoSyncParam) {
            attack = synth::envelopeNoteDivisionSeconds(attackDivParam->getIndex(), bpm);
            hold = synth::envelopeNoteDivisionSeconds(holdDivParam->getIndex(), bpm);
            decay = synth::envelopeNoteDivisionSeconds(decayDivParam->getIndex(), bpm);
            release = synth::envelopeNoteDivisionSeconds(releaseDivParam->getIndex(), bpm);
        } else {
            // Attack/Hold/Decay/Release CV (FRO285): IGNORED while tempo-synced. A synced stage's
            // effective time already comes from its *Div param and the live tempo above -- a CV
            // jack expressed in seconds has no meaning overlaid on a beat-locked division, so the
            // jack stays visibly patchable but is a no-op until the module goes back to MS mode.
            attack = modulateNormalised(*attackParam, attack, blockCV(buffer, kAttackChannel));
            hold = modulateNormalised(*holdParam, hold, blockCV(buffer, kHoldChannel));
            decay = modulateNormalised(*decayParam, decay, blockCV(buffer, kDecayChannel));
            release = modulateNormalised(*releaseParam, release, blockCV(buffer, kReleaseChannel));
        }
        return {attack, hold, decay, release};
    }

    static constexpr int MAX_VOICES = 8;
    static constexpr int kThresholdChannel = 8;
    // Stage-time/level CV jacks (FRO285), appended after Threshold -- never inserted, so a saved
    // patch that modulates Threshold on ch8 keeps doing so once these arrive on ch9-13. See
    // docs/modules/modulation.md#every-continuous-parameter-is-a-target.
    static constexpr int kAttackChannel = 9;
    static constexpr int kHoldChannel = 10;
    static constexpr int kDecayChannel = 11;
    static constexpr int kSustainChannel = 12;
    static constexpr int kReleaseChannel = 13;

    // Readout formatting for attack/hold/decay/release: below 1 s in milliseconds, at or above
    // 1 s in seconds. Decimal count shrinks as the magnitude grows so "0.10 ms" and "4999 ms"
    // both fit the knob's compact text box. valueFromString accepts an explicit "ms"/"s" suffix
    // (case-insensitive); a bare number is read as seconds, matching the parameter's own unit.
    static juce::AudioParameterFloatAttributes adsrTimeAttributes() {
        return juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction([](float v, int) {
                if (v < 1.0f) {
                    const float ms = v * 1000.0f;
                    const int decimals = ms < 10.0f ? 2 : (ms < 100.0f ? 1 : 0);
                    return juce::String(ms, decimals) + " ms";
                }
                return juce::String(v, 2) + " s";
            })
            .withValueFromStringFunction([](const juce::String& text) -> float {
                juce::String t = text.trim();
                if (t.endsWithIgnoreCase("ms"))
                    return juce::jmax(0.0f, t.dropLastCharacters(2).trim().getFloatValue() / 1000.0f);
                if (t.endsWithIgnoreCase("s"))
                    return juce::jmax(0.0f, t.dropLastCharacters(1).trim().getFloatValue());
                return juce::jmax(0.0f, t.getFloatValue());
            });
    }

    // Sustain is a LEVEL (0..1), read out as dB with 0.0 dB at unity and "-inf dB" at exactly
    // zero -- the parameter itself stays linear (see the constructor comment above).
    static juce::AudioParameterFloatAttributes adsrSustainAttributes() {
        return juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction([](float v, int) {
                if (v <= 0.0f)
                    return juce::String("-inf dB");
                return juce::String(20.0f * std::log10(v), 1) + " dB";
            })
            .withValueFromStringFunction([](const juce::String& text) -> float {
                juce::String t = text.trim();
                if (t.endsWithIgnoreCase("dB"))
                    t = t.dropLastCharacters(2).trim();
                if (t.equalsIgnoreCase("-inf") || t.equalsIgnoreCase("-infinity"))
                    return 0.0f;
                const float db = t.getFloatValue();
                return juce::jlimit(0.0f, 1.0f, std::pow(10.0f, db / 20.0f));
            });
    }

    // The three bend amounts (-1..1): not shown as their own knob (FRO112 moved them onto the
    // envelope graph's bend handles), but a host's generic automation UI still reads this.
    static juce::AudioParameterFloatAttributes adsrCurveAttributes() {
        return juce::AudioParameterFloatAttributes().withStringFromValueFunction(
            [](float v, int) { return juce::String(v, 2); });
    }

    static synth::EnvelopeParameters makeParameters(float attack, float hold, float decay, float sustain, float release,
                                                    float attackCurve, float decayCurve, float releaseCurve) {
        synth::EnvelopeParameters ep;
        ep.attack = attack;
        ep.hold = hold;
        ep.decay = decay;
        ep.sustain = sustain;
        ep.release = release;
        ep.attackCurve = attackCurve;
        ep.decayCurve = decayCurve;
        ep.releaseCurve = releaseCurve;
        return ep;
    }

    void resetMeters() {
        meterLevel.store(0.0f, std::memory_order_relaxed);
        overThreshold.store(false, std::memory_order_relaxed);
    }

    synth::EnvelopeGenerator envelopes[MAX_VOICES];
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedSustain;
    SchmittTrigger gateTriggers[MAX_VOICES];
    bool previousActive[MAX_VOICES] = {};
    std::bitset<128> heldNotes; // keyed by MIDI note number only, channel-agnostic
    int lastTriggeredVoice = 0;
    juce::AudioParameterBool* polyParam = nullptr;

    juce::AudioParameterFloat* attackParam = nullptr;
    juce::AudioParameterFloat* decayParam = nullptr;
    juce::AudioParameterFloat* sustainParam = nullptr;
    juce::AudioParameterFloat* releaseParam = nullptr;
    juce::AudioParameterFloat* holdParam = nullptr;
    juce::AudioParameterFloat* attackCurveParam = nullptr;
    juce::AudioParameterFloat* decayCurveParam = nullptr;
    juce::AudioParameterFloat* releaseCurveParam = nullptr;
    juce::AudioParameterFloat* thresholdParam = nullptr;
    juce::AudioParameterBool* tempoSyncParam = nullptr;
    juce::AudioParameterChoice* attackDivParam = nullptr;
    juce::AudioParameterChoice* holdDivParam = nullptr;
    juce::AudioParameterChoice* decayDivParam = nullptr;
    juce::AudioParameterChoice* releaseDivParam = nullptr;

    std::atomic<float> meterLevel{0.0f};
    std::atomic<float> effectiveThreshold{0.5f};
    std::atomic<bool> overThreshold{false};
    std::atomic<int> triggerCount{0};
    std::atomic<synth::EnvelopeStage> playheadStage{synth::EnvelopeStage::Idle};
    std::atomic<float> playheadProgress{0.0f};
    std::atomic<float> playheadLevel{0.0f};
    std::atomic<double> lastSeenBpm{120.0};
};
