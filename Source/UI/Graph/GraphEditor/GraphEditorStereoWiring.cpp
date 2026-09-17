// GraphEditorStereoWiring.cpp
//
// Dual-I/O device wiring, stereo-pair completion after a module is added, and module-resize
// handling (dropping cables a shrink invalidates, nudging neighbours clear). GraphEditor is
// declared in GraphEditor.h; sibling GraphEditor*.cpp files in this directory hold the rest of
// the class.

#include "GraphEditor.h"
#include "GraphEditorInternal.h"

#include "Modules/AttenuverterModule.h"
#include "Modules/AudioInputModule.h"
#include "Modules/FilterModule.h"
#include "Modules/VCAModule.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"

using namespace detail;

// MESSAGE THREAD. The audio device changed: re-point every Audio Input module at the
// engine's new input channel count, drop cables left on jacks that just disappeared, and
// re-measure the affected cards. Called from the owner's device-state-changed callback.
void GraphEditor::refreshIoModulesAfterDeviceChange() {
    // MESSAGE THREAD: the audio device changed under us, so every Audio Input card's jack count
    // may have changed with it. Pushing the engine's prepared channel count into the module here
    // (rather than waiting for the audio thread to publish it from the next block) is what makes
    // the resize immediate — and what makes it testable without a device.
    //
    // Minimal on purpose: this is only the part that must not wait, because a shrunk device leaves
    // cables on jacks that no longer exist.
    auto& graph = audioEngine.getGraph();
    const int deviceChannels = audioEngine.getDeviceInputChannelCount();
    bool sawInputModule = false;

    for (auto* node : graph.getNodes()) {
        if (node == nullptr)
            continue;
        auto* input = dynamic_cast<AudioInputModule*>(node->getProcessor());
        if (input == nullptr)
            continue;

        input->setDeviceChannelCount(deviceChannels);
        dropRoutingsOnHiddenJacks(node->nodeID);
        sawInputModule = true;

        for (auto* comp : content.getModules())
            if (comp != nullptr && comp->getNodeId() == node->nodeID)
                comp->refreshPortLayout();
    }

    if (sawInputModule)
        repaintCanvas();
}

// MESSAGE THREAD. Calls the provider above (a no-op if none is installed) and pushes the
// result into the Audio Output card's ModuleComponent, which repaints only if the text
// actually changed. Call once right after installing the provider (so the card is populated
// at startup) and again every time AudioEngine::onDeviceStateChanged fires — there is no
// timer polling this.
void GraphEditor::refreshOutputDeviceInfo() {
    // MESSAGE THREAD. The provider (installed by MainComponent) is the only thing that knows
    // Standalone-vs-Hosted framing; this just finds the card and pushes whatever it returns.
    // setOutputDeviceInfoText is itself a no-op on every module except Audio Output, so there is
    // no need to filter with isTerminalAudioSink twice — but doing it here too skips the
    // (identical, cheap) text comparison on every other card on the canvas.
    if (!outputDeviceInfoProvider)
        return;

    const juce::String text = outputDeviceInfoProvider();
    for (auto* comp : content.getModules()) {
        if (comp != nullptr && isTerminalAudioSink(comp->getModule()))
            comp->setOutputDeviceInfoText(text);
    }
}

// Re-lays every stereo-capable module already on the canvas to `dual`. Card heights do not
// move — the gutter reserves room for the dual layout in both states. See
// GraphEditorStereoWiring.cpp for why this stays separate from setDefaultDualIOForNewModules.
//
// Deliberately separate from setDefaultDualIOForNewModules: that one is also called at startup
// and whenever the Settings window opens, and retro-applying there would rewrite the user's
// patch (collapsing the factory preset's voice modules on every launch). Only a deliberate
// change of the preference calls this.
void GraphEditor::applyDualIOToExistingModules(bool dual) {
    auto& graph = audioEngine.getGraph();

    // Walks the GRAPH, not the cards. At startup the preference is restored before any
    // ModuleComponent exists — AudioEngine loads the default preset in its own constructor — so a
    // card-driven pass would silently do nothing and the patch would open with whatever layout each
    // module's constructor happened to default to.
    std::vector<juce::AudioProcessorGraph::NodeID> changed;
    for (auto* node : graph.getNodes()) {
        auto* mb = dynamic_cast<ModuleBase*>(node->getProcessor());
        if (mb == nullptr || !mb->hasDualIOParameter() || mb->isDualIO() == dual)
            continue;
        if (auto* param = findParameterByID(node->getProcessor(), "dualIO"))
            param->setValueNotifyingHost(dual ? 1.0f : 0.0f);
        changed.push_back(node->nodeID);
    }

    if (changed.empty())
        return;

    for (auto nodeId : changed) {
        // Settle through the card when there is one: it also completes L/R pairs on expand. Driven
        // straight rather than left to the parameter listener, which is asynchronous — re-laying
        // every module at once would settle a frame late and each module's cable cleanup would race
        // the next one's layout change. With no card yet (startup) the graph-side cleanup is all
        // that is needed; the first updateComponents() then builds the cards at the right size.
        ModuleComponent* card = nullptr;
        for (auto* mc : content.getModules())
            if (mc != nullptr && mc->getNodeId() == nodeId)
                card = mc;

        if (card != nullptr)
            card->applyDualIOLayoutChange();
        else
            dropHiddenRightLegConnections(nodeId);
    }
}

void GraphEditor::applyDefaultDualIOForNewModule(juce::AudioProcessor& processor,
                                                 const juce::String& moduleType) const {
    auto* mb = dynamic_cast<ModuleBase*>(&processor);
    if (mb == nullptr || !mb->hasDualIOParameter())
        return;

    // Applied in BOTH directions. It used to only ever force Dual I/O *on*, which meant the
    // preference could not express "I want single jacks" for a module whose own default is dual —
    // and the voice modules default to dual since #219. The preference is the user's stated intent
    // for anything they create, so it wins over the module's constructor default either way.
    //
    // The per-module override (Preferences → "Per-module I/O defaults...") wins over the global
    // default when the two disagree — it exists specifically to say "everything follows the
    // toggle EXCEPT this one type". A type with no entry in the map is untouched by the override
    // and falls through to the global default, same as before that popup existed.
    bool dual = defaultDualIOForNewModules;
    if (auto it = dualIOPerModuleOverrides.find(moduleType); it != dualIOPerModuleOverrides.end())
        dual = it->second;

    if (auto* param = findParameterByID(&processor, "dualIO"))
        param->setValueNotifyingHost(dual ? 1.0f : 0.0f);
}

// Dual I/O only remaps visible jacks onto raw ch0/ch1. A collapsed Audio cable that only
// landed on the left leg (typical when the far end is Audio Output, which is not ModuleBase)
// is completed to L→L / R→R so toggling Dual I/O on shows both jacks wired.
void GraphEditor::completeStereoPairConnections(ModuleComponent* moduleComp) {
    if (moduleComp == nullptr || moduleComp->getModule() == nullptr)
        return;

    auto* mb = dynamic_cast<ModuleBase*>(moduleComp->getModule());
    if (mb == nullptr || !mb->hasDualIOParameter())
        return;

    auto& graph = audioEngine.getGraph();
    const auto nodeId = moduleComp->getNodeId();
    if (graph.getNodeForId(nodeId) == nullptr)
        return;

    auto hasEdge = [&](juce::AudioProcessorGraph::NodeID src, int srcCh, juce::AudioProcessorGraph::NodeID dst,
                       int dstCh) {
        for (const auto& c : graph.getConnections())
            if (c.source.nodeID == src && c.source.channelIndex == srcCh && c.destination.nodeID == dst &&
                c.destination.channelIndex == dstCh)
                return true;
        return false;
    };

    auto inputFeedCount = [&](int rawChannel) {
        int n = 0;
        for (const auto& c : graph.getConnections())
            if (c.destination.nodeID == nodeId && !c.destination.isMIDI() && c.destination.channelIndex == rawChannel)
                ++n;
        return n;
    };
    auto inputChannelIsFed = [&](int rawChannel) { return inputFeedCount(rawChannel) > 0; };

    const int myOutputLeg = rightAudioLegOf(mb, /*asInput=*/false);
    const int myInputLeg = rightAudioLegOf(mb, /*asInput=*/true);

    // MIGRATE BEFORE WIRING. An audio input that was one jack a moment ago can already carry TWO
    // feeds from the same upstream node: that is the summed cable the upstream's own split put there
    // (its Audio R aimed at our mono jack), or two raws of one collapsed jack hand-wired onto it.
    // Splitting gives that second feed a jack of its own, so it MOVES — anything else leaves it in
    // place and hangs a third cable off the same pair, which is exactly what the reported
    // Osc-then-Filter sequence produced: L->L, the old R->L sum, and a new R->R on top.
    //
    // Only a pair from the SAME upstream node migrates. Two feeds from two different modules are a
    // mix the user built by hand, and moving half of it somewhere else would be rewriting their
    // patch; that case falls through untouched (and, having more than one feed, takes neither the
    // right-leg wire nor the broadcast below).
    if (myInputLeg > 0 && mb->isDualIO() && !inputChannelIsFed(myInputLeg)) {
        std::vector<juce::AudioProcessorGraph::Connection> leftFeeds;
        for (const auto& c : graph.getConnections())
            if (c.destination.nodeID == nodeId && !c.destination.isMIDI() && c.destination.channelIndex == 0)
                leftFeeds.push_back(c);

        if (leftFeeds.size() > 1) {
            const juce::AudioProcessorGraph::Connection* migrate = nullptr;
            for (const auto& candidate : leftFeeds) {
                int fromSameNode = 0;
                for (const auto& other : leftFeeds)
                    if (other.source.nodeID == candidate.source.nodeID)
                        ++fromSameNode;
                if (fromSameNode < 2)
                    continue;

                // Prefer the feed that comes off the upstream's RIGHT leg, so the pair lands
                // L->L / R->R rather than crossed. Falling back to any second feed from that node
                // covers the collapsed-jack case, where both raws belong to one visible jack.
                auto* srcNode = graph.getNodeForId(candidate.source.nodeID);
                const int srcLeg =
                    srcNode != nullptr ? rightAudioLegOf(srcNode->getProcessor(), /*asInput=*/false) : -1;
                if (candidate.source.channelIndex == srcLeg) {
                    migrate = &candidate;
                    break;
                }
                if (migrate == nullptr && candidate.source.channelIndex != 0)
                    migrate = &candidate;
            }

            if (migrate != nullptr) {
                const juce::AudioProcessorGraph::Connection moved{migrate->source, {nodeId, myInputLeg}};
                graph.removeConnection(*migrate);
                if (!hasEdge(moved.source.nodeID, moved.source.channelIndex, nodeId, myInputLeg))
                    graph.addConnection(moved);
            }
        }
    }

    const auto connections = graph.getConnections();
    for (const auto& c : connections) {
        if (c.source.isMIDI() || c.destination.isMIDI())
            continue;

        // Outgoing: our left leg is patched to dest's left leg, but the right pair is not —
        // complete L→L / R→R using each end's own right-leg channel.
        if (c.source.nodeID == nodeId && c.source.channelIndex == 0 && c.destination.channelIndex == 0 &&
            myOutputLeg >= 0) {
            auto* destNode = graph.getNodeForId(c.destination.nodeID);
            const int destLeg = destNode != nullptr ? rightAudioLegOf(destNode->getProcessor(), /*asInput=*/true) : -1;
            if (destLeg >= 0 && !hasEdge(nodeId, myOutputLeg, destNode->nodeID, destLeg))
                graph.addConnection({{nodeId, myOutputLeg}, {destNode->nodeID, destLeg}});

            // Our right leg now feeds the dest's right leg, so the LEFT leg must stop feeding it as
            // well. While we were collapsed, resolvePolyLink duplicated our one Audio jack onto both
            // of the dest's raw legs (the mono→collapsed-pair fan); leaving that edge in place after
            // the split sums L+R into the dest's right leg — audibly right-heavy, and invisible,
            // since both edges draw as the same cable. Mirror of the re-point in
            // dropHiddenRightLegConnections, which is what puts the duplicate back on collapse.
            if (destLeg > 0 && destNode != nullptr && hasEdge(nodeId, myOutputLeg, destNode->nodeID, destLeg))
                graph.removeConnection({{nodeId, 0}, {destNode->nodeID, destLeg}});

            // destLeg < 0 means the destination has no SECOND audio input the user can see — a
            // collapsed split-block module (Filter/VCA/Wavetable), whose one "Audio" jack is its left
            // leg alone. USER RULING: wire our right leg into that same mono jack anyway, as an
            // explicit summed second cable. A module the user just split must not come up with a
            // visibly dangling Audio R, and stereo-into-mono summing is what hand-wiring both legs
            // onto that jack already produces — connectPorts has always allowed summed inputs.
            //
            // While both legs still carry the identical signal (the usual state right after a split)
            // the sum is +6 dB. That is transient: it lasts only until the legs differ, which is the
            // point of splitting. This was left dangling before precisely to avoid that jump; the
            // ruling traded it for both jacks being wired.
            //
            // What does NOT change: never wire the destination's hidden kRightBase. That block has no
            // jack, so the cable would be audible and impossible to unplug — the invariant
            // dropHiddenRightLegConnections exists to enforce. Here the target is
            // c.destination.channelIndex, the very channel our left leg is already wired to, so it is
            // visible by construction.
            //
            // Confined to split-block modules on purpose: their right leg is hidden again on
            // collapse, so dropHiddenRightLegConnections removes this extra cable for free and the
            // on/off round-trip is exact. An FX's raw1 stays part of its collapsed jack, so the same
            // cable would survive a collapse and be indistinguishable from a hand-drawn one.
            // (myOutputLeg >= 0 on a split-block module already implies it is currently dual: its
            // kRightBase is only reachable from a visible jack in that state.)
            if (destLeg < 0 && destNode != nullptr && mb->hasSplitBlockStereo() &&
                !hasEdge(nodeId, myOutputLeg, destNode->nodeID, c.destination.channelIndex))
                graph.addConnection({{nodeId, myOutputLeg}, {destNode->nodeID, c.destination.channelIndex}});
        }

        // Outgoing into a DEDICATED mono audio input, i.e. a jack that is nobody's stereo pair: the
        // Ring Modulator's Carrier (ch0) and Modulator (ch1) are the case that reached a user, since
        // their roles differ and ch1 is never a right-hand input. The branch above only ever looked at
        // cables landing on the destination's ch0, so a module split while feeding Modulator wired
        // Audio L and stopped. Same ruling as the collapsed mono jack: sum the right leg into the very
        // same input.
        if (c.source.nodeID == nodeId && c.source.channelIndex == 0 && c.destination.channelIndex != 0 &&
            myOutputLeg >= 0 && mb->hasSplitBlockStereo()) {
            auto* destNode = graph.getNodeForId(c.destination.nodeID);
            const int destCh = c.destination.channelIndex;

            // Qualifies only when the target is a standalone mono AUDIO input the user can see:
            //   * a real ModuleBase jack (graph I/O has no roles to ask, and its ch1 is a stereo leg),
            //   * reachable from a visible jack, so never a hidden block,
            //   * not modulation CV - our left leg being patched there does not license dumping an
            //     audio-rate copy onto a Cutoff or Rate jack as well,
            //   * span 1 and not the right leg of a pair, which the ch0 branch above already covers.
            bool qualifies = false;
            if (destNode != nullptr) {
                if (auto* destMb = dynamic_cast<ModuleBase*>(destNode->getProcessor())) {
                    const LogicalPort port = destMb->mapInputChannel(destCh);
                    qualifies = audioChannelReachableFromJack(*destMb, destCh, /*isInput=*/true) &&
                                port.role != PortRole::ModCV && !destMb->isAutoPromotableModTarget(destCh) &&
                                port.polyVoiceSpan == 1 && rightAudioLegOf(destMb, /*asInput=*/true) != destCh;
                }
            }

            if (qualifies && !hasEdge(nodeId, myOutputLeg, destNode->nodeID, destCh))
                graph.addConnection({{nodeId, myOutputLeg}, {destNode->nodeID, destCh}});
        }

        // Incoming: source's left leg is patched to ours, and the source is itself a stereo pair.
        //
        // Gated on the left jack having exactly ONE feed, which is what it has after the migration
        // above did its work. More than one means a mix of two different modules that migration
        // deliberately left alone: pairing or broadcasting one of them onto Audio R would pick a
        // winner by cable order and leave the legs carrying different mixes.
        if (c.destination.nodeID == nodeId && c.destination.channelIndex == 0 && c.source.channelIndex == 0 &&
            myInputLeg >= 0 && inputFeedCount(0) == 1) {
            auto* srcNode = graph.getNodeForId(c.source.nodeID);
            const int srcLeg = srcNode != nullptr ? rightAudioLegOf(srcNode->getProcessor(), /*asInput=*/false) : -1;
            if (srcLeg >= 0 && !hasEdge(srcNode->nodeID, srcLeg, nodeId, myInputLeg))
                graph.addConnection({{srcNode->nodeID, srcLeg}, {nodeId, myInputLeg}});

            // Same de-duplication from the other end: once the source's own right leg reaches our
            // right input, its left leg must not still be wired there too.
            if (srcLeg > 0 && srcNode != nullptr && hasEdge(srcNode->nodeID, srcLeg, nodeId, myInputLeg))
                graph.removeConnection({{srcNode->nodeID, 0}, {nodeId, myInputLeg}});

            // MONO-ONLY UPSTREAM: the source has no second audio output the user can see — either a
            // collapsed split-block module (its "Audio" jack is the left leg alone) or a genuinely
            // mono one. Broadcast the feed our LEFT leg already has onto the right leg, so a module
            // the user just split arrives with both legs live rather than half-wired.
            //
            // This is the one place the mono broadcast is applied to a non-adjacent right leg, and it
            // is deliberately confined to this handler: flipping the toggle is an explicit user
            // action on one module, whereas resolvePolyLink governs manual cable drags and
            // smart-connect, where the established behaviour for a split-block pair is left-leg-only
            // (see its comment). Nothing here widens that.
            //
            // Copying a feed cannot change the mix at all: the source simply drives one more
            // destination at the same level. (The output side above sums instead, which is why it
            // needed a ruling and this did not.) Guarded on the right leg being unfed so a second
            // toggle cannot stack feeds, and it takes the channel that already feeds our left audio
            // input rather than assuming ch0 is audio on the peer - the user routed that edge.
            if (srcLeg < 0 && srcNode != nullptr && !inputChannelIsFed(myInputLeg))
                graph.addConnection({{srcNode->nodeID, c.source.channelIndex}, {nodeId, myInputLeg}});
        }
    }

    dropHiddenRightLegConnections(nodeId);
}

// The raw channel carrying `proc`'s right audio leg for wiring purposes, or -1 when it has none
// the user can reach. Asks the module (FX use ch1, split-block modules their own kRightBase),
// then requires the channel to be reachable from a VISIBLE jack — a collapsed split-block
// module still reports PortRole::Audio on its hidden block, and wiring that would create a
// cable nobody can unplug. Static so tests can pin it directly.
int GraphEditor::rightAudioLegOf(juce::AudioProcessor* proc, bool asInput) {
    // Which raw channel is this end's right leg? FX put it on ch1; the voice modules put it on their
    // own kRightBase block, so asking the module beats assuming ch1 — assuming would have wired an
    // Oscillator's Waveform CV channel as if it were audio.
    if (proc == nullptr)
        return -1;
    auto* peerMb = dynamic_cast<ModuleBase*>(proc);
    if (peerMb == nullptr) {
        // Graph I/O (Audio Input/Output) is a plain contiguous pair.
        const int channels = asInput ? proc->getTotalNumInputChannels() : proc->getTotalNumOutputChannels();
        return channels >= 2 ? 1 : -1;
    }
    const int leg = peerMb->rightAudioLegChannel();
    if (leg < 0)
        return -1;
    const int channels = asInput ? proc->getTotalNumInputChannels() : proc->getTotalNumOutputChannels();
    if (leg >= channels)
        return -1;
    const LogicalPort port = asInput ? peerMb->mapInputChannel(leg) : peerMb->mapOutputChannel(leg);
    if (port.role != PortRole::Audio)
        return -1;

    // ...and it has to be a leg the user can actually SEE. A collapsed split-block module keeps
    // rendering its kRightBase block but exposes no jack for it, and FilterModule/VCAModule report
    // PortRole::Audio for those channels either way — so a role check alone would let the toggle
    // wire a cable onto a hidden jack, which is exactly the routing the invariant in CLAUDE.md
    // ("an invisible jack cannot be unplugged") forbids. A collapsed FX pair still passes: its
    // single Audio jack owns raw ch1 through voiceSpan 2.
    return audioChannelReachableFromJack(*peerMb, leg, asInput) ? leg : -1;
}

// True when `rawChannel` is covered by one of the module's currently VISIBLE jacks (a jack's
// JackTarget spans `voiceSpan` consecutive raw channels, which is how a collapsed FX jack owns
// both of its legs). The wiring-side counterpart of handleModuleResized's exposure check.
bool GraphEditor::audioChannelReachableFromJack(const ModuleBase& mb, int rawChannel, bool isInput) {
    const int visible = isInput ? mb.getVisibleInputPortCount() : mb.getVisibleOutputPortCount();
    for (int jack = 0; jack < visible; ++jack)
        for (const auto& t : mb.getJackTargets(jack, isInput))
            for (int v = 0; v < t.voiceSpan; ++v)
                if (t.rawHeadChannel + v == rawChannel)
                    return true;
    return false;
}

// Unhooks a collapsed split-block module's hidden right leg, RE-POINTING each cable onto the
// matching channel of the surviving left block wherever the far end still exposes it (and
// simply dropping it where it does not). Graph-level, so it works before the cards exist.
// No-op for FX pairs, whose collapsed jack legitimately still owns both raw legs. See
// GraphEditorStereoWiring.cpp for the collapse-level rationale.
//
// The re-point is what keeps a collapse level across the stereo field: without it, collapsing
// the default patch's VCA starved the whole FX tail's right channel and the mix jumped left.
void GraphEditor::dropHiddenRightLegConnections(juce::AudioProcessorGraph::NodeID nodeId) {
    auto& graph = audioEngine.getGraph();
    auto* node = graph.getNodeForId(nodeId);
    if (node == nullptr)
        return;

    auto* mb = dynamic_cast<ModuleBase*>(node->getProcessor());
    if (mb == nullptr)
        return;

    // Collapsing a SPLIT-BLOCK module hides its right leg entirely — unlike an FX pair, where the
    // collapsed jack still owns both raw legs. An invisible jack cannot be unplugged, so anything
    // left wired to that block has to be dropped here or it becomes a cable whose effect the user
    // can hear but never reach. (Only for split-block layouts: dropping ch1 on an FX module would
    // silently break the perfectly valid collapsed stereo pair.)
    if (mb->isDualIO() || !mb->hasSplitBlockStereo())
        return;

    const int hiddenBase = mb->rightAudioLegChannel();
    const int inputCount = mb->getTotalNumInputChannels();
    const int outputCount = mb->getTotalNumOutputChannels();

    auto hasEdge = [&graph](const juce::AudioProcessorGraph::Connection& c) {
        for (const auto& existing : graph.getConnections())
            if (existing == c)
                return true;
        return false;
    };

    // Does anything at all feed this raw input channel of ours? Used below to decide whether a
    // dropped right-leg INPUT cable has a left leg to fall back onto or would double-feed it.
    auto inputChannelIsFed = [&graph, nodeId](int rawChannel) {
        for (const auto& c : graph.getConnections())
            if (c.destination.nodeID == nodeId && !c.destination.isMIDI() && c.destination.channelIndex == rawChannel)
                return true;
        return false;
    };

    for (const auto& c : graph.getConnections()) {
        if (c.source.isMIDI() || c.destination.isMIDI())
            continue;
        const bool fromHiddenOutput =
            c.source.nodeID == nodeId && c.source.channelIndex >= hiddenBase && c.source.channelIndex < outputCount;
        const bool intoHiddenInput = c.destination.nodeID == nodeId && c.destination.channelIndex >= hiddenBase &&
                                     c.destination.channelIndex < inputCount;
        if (!fromHiddenOutput && !intoHiddenInput)
            continue;

        graph.removeConnection(c);

        // RE-POINT, don't just delete. Collapsing hides the right block, but the cable hanging off
        // it was drawn by the user and its far end is still there — so it moves to the leg that
        // survives, which is the same raw offset in the LEFT block (voice v of the right block
        // pairs with voice v of the left one).
        //
        // This is the missing link behind "toggling Dual I/O off biases the mix left": in the
        // default patch the VCA's right leg feeds Distortion ch1, and dropping that cable left the
        // whole collapsed FX tail (Distortion → Delay → Reverb → Audio Output) rendering silence on
        // every right channel. Re-pointing sends the surviving mono leg there instead, at UNITY —
        // ModuleBase::panGains keeps a centred module's left leg at full level, so a collapsed
        // module is exactly as loud in both channels as it was in the left one before (no gain
        // compensation, and none wanted: an equal-power law would have made it 3 dB quiet).
        if (fromHiddenOutput) {
            const int leftCh = c.source.channelIndex - hiddenBase;
            auto* destNode = graph.getNodeForId(c.destination.nodeID);
            if (destNode == nullptr || !audioChannelReachableFromJack(*mb, leftCh, /*isInput=*/false))
                continue;
            // The far end must still expose the channel we are re-pointing onto — a collapsed peer's
            // own hidden right block is not a legal target, and neither end may gain a cable it
            // cannot show.
            if (auto* destMb = dynamic_cast<ModuleBase*>(destNode->getProcessor())) {
                if (!audioChannelReachableFromJack(*destMb, c.destination.channelIndex, /*isInput=*/true))
                    continue;
            } else if (c.destination.channelIndex >= destNode->getProcessor()->getTotalNumInputChannels()) {
                continue;
            }
            const juce::AudioProcessorGraph::Connection moved{{nodeId, leftCh}, c.destination};
            if (!hasEdge(moved))
                graph.addConnection(moved);
        } else {
            // Input side: our left leg is normally already fed by the same upstream, and adding a
            // second feed there would sum L+R into one mono jack (+6 dB). So re-point ONLY when the
            // left leg has nothing at all — otherwise the collapse legitimately drops the cable.
            const int leftCh = c.destination.channelIndex - hiddenBase;
            if (!audioChannelReachableFromJack(*mb, leftCh, /*isInput=*/true) || inputChannelIsFed(leftCh))
                continue;
            const juce::AudioProcessorGraph::Connection moved{c.source, {nodeId, leftCh}};
            if (!hasEdge(moved))
                graph.addConnection(moved);
        }
    }
}

// A module changed footprint in place (the Macro bank, when its "Knobs" count changes).
// Drops any routing left on an output jack that is no longer visible, then pushes overlapping
// neighbours clear. The resized module itself never moves.
void GraphEditor::handleModuleResized(ModuleComponent* moduleComp) {
    if (moduleComp == nullptr || moduleComp->getModule() == nullptr)
        return;

    auto& graph = audioEngine.getGraph();
    const auto nodeId = moduleComp->getNodeId();
    if (graph.getNodeForId(nodeId) == nullptr)
        return;

    // 1. Jacks that just disappeared take their cables with them. Leaving them connected would
    //    mean a routing that still shows in the mod matrix, still costs a node, and no longer
    //    carries anything (the module silences hidden channels) — with no jack to unplug it from.
    //    No undo transaction is opened here: the parameter gesture that changed the count already
    //    snapshots the whole graph before and after, so this is part of that single undo step.
    if (auto* mb = dynamic_cast<ModuleBase*>(moduleComp->getModule())) {
        auto channelStillExposed = [mb](int rawChannel, bool isInput) {
            const int visible = isInput ? mb->getVisibleInputPortCount() : mb->getVisibleOutputPortCount();
            for (int j = 0; j < visible; ++j) {
                for (const auto& t : mb->getJackTargets(j, isInput))
                    for (int v = 0; v < t.voiceSpan; ++v)
                        if (t.rawHeadChannel + v == rawChannel)
                            return true;
            }
            return false;
        };

        std::vector<juce::AudioProcessorGraph::Connection> toRemove;

        for (const auto& c : graph.getConnections()) {
            if (c.source.nodeID == nodeId && !c.source.isMIDI() && !channelStillExposed(c.source.channelIndex, false)) {
                if (auto* dstNode = graph.getNodeForId(c.destination.nodeID)) {
                    if (dynamic_cast<AttenuverterModule*>(dstNode->getProcessor()) != nullptr) {
                        audioEngine.removeModRouting(dstNode->nodeID);
                        continue;
                    }
                }
                toRemove.push_back(c);
            }
            if (c.destination.nodeID == nodeId && !c.destination.isMIDI() &&
                !channelStillExposed(c.destination.channelIndex, true)) {
                if (auto* srcNode = graph.getNodeForId(c.source.nodeID)) {
                    if (dynamic_cast<AttenuverterModule*>(srcNode->getProcessor()) != nullptr) {
                        audioEngine.removeModRouting(srcNode->nodeID);
                        continue;
                    }
                }
                toRemove.push_back(c);
            }
        }

        for (const auto& c : toRemove)
            graph.removeConnection(c);
    }

    // 2. Nudge neighbours clear of the new footprint. The resized module stays put.
    std::vector<synth::LayoutUtil::Box> boxes;
    for (auto* comp : content.getModules())
        if (comp != nullptr)
            boxes.push_back({comp->getNodeId(), comp->getBounds()});

    for (const auto& moved : synth::LayoutUtil::resolveOverlapsAfterResize(nodeId, boxes)) {
        for (auto* comp : content.getModules()) {
            if (comp == nullptr || comp->getNodeId() != moved.id)
                continue;
            comp->setTopLeftPosition(moved.pos);
            if (auto* node = graph.getNodeForId(moved.id)) {
                node->properties.set("x", moved.pos.x);
                node->properties.set("y", moved.pos.y);
            }
        }
    }

    repaintCanvas();
}
