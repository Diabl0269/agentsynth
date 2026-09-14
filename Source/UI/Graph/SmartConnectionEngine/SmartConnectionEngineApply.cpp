// SmartConnectionEngineApply.cpp
//
// Building and applying smart-connection suggestions (refreshSmartSuggestions,
// applySmartSuggestions). Sibling SmartConnectionEngine.cpp holds the naming and eligibility
// helpers this depends on. SmartConnectionEngine is declared in SmartConnectionEngine.h; this TU
// includes the real GraphEditor.h for GraphEditor::resolvePolyLink/estimatePortCenter (both stay
// on GraphEditor — pure, host-independent) and the shared detail:: helpers in
// GraphEditorInternal.h.

#include "SmartConnectionEngine.h"

#include "Modules/AttenuverterModule.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/GraphEditor/GraphEditorInternal.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"

using namespace detail;

void SmartConnectionEngine::refreshSmartSuggestions(const DragPreviewState& drag) {
    const auto previous = smartSuggestions_;
    smartSuggestions_.clear();

    if (!drag.active || drag.ghost.isEmpty() || !shouldOfferSmartConnections(drag)) {
        if (previous != smartSuggestions_)
            host_.repaintCanvas();
        return;
    }

    juce::AudioProcessor* ghostProc = nullptr;
    if (drag.selfId.uid != 0) {
        if (auto* mc = host_.moduleComponentFor(drag.selfId))
            ghostProc = mc->getModule();
    } else {
        ghostProc = drag.probe;
    }
    if (ghostProc == nullptr) {
        if (previous != smartSuggestions_)
            host_.repaintCanvas();
        return;
    }

    const auto ghostBounds = drag.ghost;
    // Where the cursor is pointing, before anti-overlap relocated the card. Empty on paths that
    // never set it (older tests drive updateDragPreview directly), in which case candidacy falls
    // back to the landing rect exactly as before.
    const auto aimBounds = drag.aim;

    struct Candidate {
        SmartSuggestion suggestion;
        int score = 0;
        float distance = 0.0f;
        bool isMidi = false;
    };
    std::vector<Candidate> audioCandidates;
    std::vector<Candidate> midiCandidates;

    /** The reroute shared by every surviving jack pair of one insert group. Group-wide rather than
     *  per-leg: the cable sets must survive the fan dedupe that drops redundant pairs. */
    struct InsertPlan {
        juce::AudioProcessorGraph::NodeID upstreamId{};
        std::vector<SmartSuggestion::InsertLink> doomedLinks;
        std::vector<SmartSuggestion::InsertLink> upstreamCables;
        std::vector<SmartSuggestion::InsertLink> upstreamPreviewLegs;
        synth::ui::ModuleCategory upstreamCategory = synth::ui::ModuleCategory::Utility;
    };

    auto componentForNode = [this](juce::AudioProcessorGraph::NodeID id) -> ModuleComponent* {
        return host_.moduleComponentFor(id);
    };

    const bool ghostAcceptsMidi = ghostProc->acceptsMidi();
    const bool ghostProducesMidi = ghostProc->producesMidi();

    for (auto* neighbor : host_.modules()) {
        if (neighbor == nullptr || neighbor->getModule() == nullptr)
            continue;
        if (neighbor->getNodeId() == drag.selfId)
            continue;
        // Hidden attenuverter nodes are never smart-wired.
        if (dynamic_cast<AttenuverterModule*>(neighbor->getModule()) != nullptr)
            continue;

        const auto neighborBounds = neighbor->getBounds();
        // Cheap cull: facing jacks cannot be closer than the modules themselves. Measured against
        // BOTH where the card will land and where the cursor is aiming, whichever is closer — a
        // ghost aimed into a gap gets pushed clear by anti-overlap, and only the aim reflects what
        // the user meant. The jack-level test below still gates ordinary suggestions, so admitting
        // an aim-based candidate here does not by itself create one.
        const float landingDist = edgeToEdgeDistance(ghostBounds.toFloat(), neighborBounds.toFloat());
        const float aimDist =
            aimBounds.isEmpty() ? landingDist : edgeToEdgeDistance(aimBounds.toFloat(), neighborBounds.toFloat());
        if (std::min(landingDist, aimDist) > kSmartConnectionProximityPx)
            continue;

        auto* neighborProc = neighbor->getModule();
        const bool requireSourceFree = smartConnectionMode_ == SmartConnectionMode::NewAndUnwired;

        auto jackPoint = [&](bool fromGhost, int jack, bool isInput, bool isMidi) -> juce::Point<float> {
            if (fromGhost)
                return GraphEditor::estimatePortCenter(ghostProc, ghostBounds, jack, isInput, isMidi).toFloat();
            if (isMidi)
                return (neighbor->getBounds().getPosition() + neighbor->getMidiPortCenter(!isInput)).toFloat();
            return (neighbor->getBounds().getPosition() + neighbor->getPortCenter(jack, isInput)).toFloat();
        };

        /** The cables one connectPorts call actually draws, so a preview can never claim less than
         *  the drop will wire. Walks the SAME PolyLink connectPorts walks, maps each raw pair back
         *  to its visible jacks, and dedupes: several raw edges through one jack pair are one cable,
         *  but a collapsed jack fanning onto a destination that fronts those raws separately is two.
         *  Endpoints come from caller-supplied providers because either end can be the ghost, the
         *  neighbour, or (for an insert's upstream leg) a third card entirely. */
        using JackPointFn = std::function<juce::Point<float>(int jack, bool isInput)>;
        auto resolveDrawnLegs = [](juce::AudioProcessor* sProc, int sJack, juce::AudioProcessor* dProc, int dJack,
                                   const JackPointFn& srcPointFor, const JackPointFn& dstPointFor) {
            std::vector<SmartSuggestion::InsertLink> legs;
            auto* sMb = dynamic_cast<ModuleBase*>(sProc);
            auto* dMb = dynamic_cast<ModuleBase*>(dProc);
            const auto link = GraphEditor::resolvePolyLink(sMb, sJack, dMb, dJack);
            for (int v = 0; v < link.voiceCount; ++v) {
                const int rawSrc = link.sourceRawChannel + v * link.sourceStride;
                const int rawDst = link.destRawChannel + v;
                SmartSuggestion::InsertLink leg;
                leg.fromJack = sMb != nullptr ? sMb->mapOutputChannel(rawSrc).visibleJackIndex : rawSrc;
                leg.toJack = dMb != nullptr ? dMb->mapInputChannel(rawDst).visibleJackIndex : rawDst;
                if (std::find(legs.begin(), legs.end(), leg) != legs.end())
                    continue; // same drawn cable, just another raw edge inside it
                leg.p1 = srcPointFor(leg.fromJack, false);
                leg.p2 = dstPointFor(leg.toJack, true);
                legs.push_back(leg);
            }
            return legs;
        };

        auto pushAudioGroup = [&](bool ghostIsSource, juce::AudioProcessor* srcProc, juce::AudioProcessor* dstProc,
                                  juce::AudioProcessorGraph::NodeID srcNodeIdForFreeCheck,
                                  juce::AudioProcessorGraph::NodeID dstNodeIdForFreeCheck, bool checkDstFree) {
            const auto srcLegs = collectSmartAudioLegs(srcProc, false);
            const auto dstLegs = collectSmartAudioLegs(dstProc, true);
            auto pairs = expandAudioJackPairs(srcLegs, dstLegs);
            if (pairs.empty())
                return;

            auto* srcMb = dynamic_cast<ModuleBase*>(srcProc);
            auto* dstMb = dynamic_cast<ModuleBase*>(dstProc);

            // Drop pairs that target mod-CV or are already connected.
            pairs.erase(std::remove_if(pairs.begin(), pairs.end(),
                                       [&](const std::pair<int, int>& pr) {
                                           if (audioJackIsModCvDest(dstMb, pr.second))
                                               return true;
                                           if (drag.selfId.uid == 0)
                                               return false;
                                           const auto srcId = ghostIsSource ? drag.selfId : neighbor->getNodeId();
                                           const auto dstId = ghostIsSource ? neighbor->getNodeId() : drag.selfId;
                                           return areJacksAlreadyConnected(srcId, pr.first, dstId, pr.second, false);
                                       }),
                        pairs.end());
            if (pairs.empty())
                return;

            // An INSERT is aimed differently from a new cable, so the jack-level test below does not
            // apply to it. Both of its halves assume the ghost sits clear to the LEFT of the card it
            // is being wired into — true for a new cable, false for an insert, where the natural aim
            // is the gap between two wired cards or the doomed cable itself. There the ghost
            // OVERLAPS its destination: its output jack is inside (or past) the destination's left
            // edge, so the flow rule rejects every pair and the jack distance blows past the cap.
            //
            // For an insert we lean on the module-level proximity cull above (which an overlapping
            // ghost passes at distance 0) plus one guard: the ghost's CENTRE must not be past the
            // destination's right edge. Dragged clean past a card is not "insert into it" — and that
            // guard is also what stops an insert being offered into a card the ghost has already
            // moved beyond, e.g. the upstream it is being spliced in after. Final geometry is
            // findFreeSlot's business either way, so a transiently overlapping ghost is harmless.
            const auto& insertAim = aimBounds.isEmpty() ? ghostBounds : aimBounds;
            const bool relaxFlowForInsert =
                ghostIsSource && isInsertModifierDown() && insertAim.getCentreX() <= neighborBounds.getRight();

            const size_t beforeProximity = pairs.size();
            if (!relaxFlowForInsert) {
                // Jack-to-jack proximity + left-to-right flow: a module on the right must not wrap
                // its outputs around to the dragged module's left inputs.
                pairs.erase(std::remove_if(pairs.begin(), pairs.end(),
                                           [&](const std::pair<int, int>& pr) {
                                               const auto srcPt = jackPoint(ghostIsSource, pr.first, false, false);
                                               const auto dstPt = jackPoint(!ghostIsSource, pr.second, true, false);
                                               if (srcPt.x > dstPt.x + 8.0f)
                                                   return true;
                                               return srcPt.getDistanceFrom(dstPt) > kSmartConnectionProximityPx;
                                           }),
                            pairs.end());
                if (pairs.empty())
                    return;
                // Stereo / fan groups: both-or-neither on proximity, same as occupancy.
                if (beforeProximity >= 2 && pairs.size() != beforeProximity)
                    return;
            }

            // What an already-occupied destination jack means depends on the modifier and the node:
            //
            //   * Cmd held  -> INSERT IN SERIES, at ANY module. The upstream cabling is rerouted
            //                 through the ghost. This is the only way to insert; nothing inserts
            //                 without the modifier.
            //   * No Cmd, terminal audio sink -> plain ADDITIVE parallel connection. The sink is
            //                 wired in essentially every real patch, so a hard stop there means a
            //                 module parked next to it can never be offered anything; and summing
            //                 into the mix bus is exactly what dragging a cable there by hand does.
            //                 Existing cables are left alone.
            //   * No Cmd, any other module -> hard stop, unchanged. Silently summing into a jack the
            //                 user wired mid-patch is never something to suggest.
            //
            // insertPlan is set for the whole group, or left empty for an ordinary add.
            std::optional<InsertPlan> insertPlan;
            if (checkDstFree && dstNodeIdForFreeCheck.uid != 0) {
                std::set<int> uniqueDsts;
                for (const auto& pr : pairs)
                    uniqueDsts.insert(pr.second);
                bool anyOccupied = false;
                for (int d : uniqueDsts) {
                    if (!isInputJackFree(dstNodeIdForFreeCheck, d, false))
                        anyOccupied = true;
                }

                // Proximity to the destination's INPUT side is already what gates us here: the
                // jack-to-jack filter above measures the ghost's output jack against
                // jackPoint(dstJack, isInput=true), and rejects a source sitting to the right of it.
                if (anyOccupied && !isInsertModifierDown()) {
                    // Parallel add is offered at the terminal sink only.
                    if (!(ghostIsSource && isTerminalAudioSink(dstProc)))
                        return;
                } else if (anyOccupied) {
                    // A ghost with no audio input cannot go in series — there would be nothing for
                    // the rerouted upstream to feed.
                    const auto ghostInLegs = collectSmartAudioLegs(ghostProc, true);
                    if (!ghostIsSource || ghostInLegs.empty())
                        return;

                    // Both-or-neither, mirroring the stereo group rule above: every leg of the group
                    // must be fed by one and the same upstream node, or nothing is offered. A mix of
                    // free and occupied legs, or two different feeds, would change the summing.
                    std::optional<juce::AudioProcessorGraph::NodeID> upstreamNode;
                    std::unordered_map<int, std::vector<int>> upstreamJacksForDst;
                    for (int d : uniqueDsts) {
                        const auto up = findSingleUpstreamAudioLink(dstNodeIdForFreeCheck, d);
                        if (!up.has_value() || up->jacks.empty() || up->nodeId == drag.selfId)
                            return;
                        if (upstreamNode.has_value() && *upstreamNode != up->nodeId)
                            return;
                        upstreamNode = up->nodeId;
                        upstreamJacksForDst[d] = up->jacks;
                    }

                    auto* upstreamComp = upstreamNode.has_value() ? componentForNode(*upstreamNode) : nullptr;
                    if (upstreamComp == nullptr || upstreamComp->getModule() == ghostProc)
                        return;
                    auto upstreamCategory = synth::ui::ModuleCategory::Utility;
                    if (auto* umb = dynamic_cast<ModuleBase*>(upstreamComp->getModule()))
                        upstreamCategory = synth::ui::categoryFor(umb->getModuleType());

                    auto* upstreamMb = dynamic_cast<ModuleBase*>(upstreamComp->getModule());
                    auto* ghostMb = dynamic_cast<ModuleBase*>(ghostProc); // == srcMb here (ghostIsSource)
                    auto upstreamJackPoint = [&](int jack) {
                        return (upstreamComp->getBounds().getPosition() +
                                upstreamComp->getPortCenter(jack, /*isInput=*/false))
                            .toFloat();
                    };

                    InsertPlan plan;
                    plan.upstreamId = *upstreamNode;
                    plan.upstreamCategory = upstreamCategory;

                    // EVERY occupied sink jack has a doomed cable, collected here — before and
                    // independently of the fan dedupe below. Hanging these off the surviving pairs
                    // instead would lose the link of any pair the dedupe drops, and the cable it
                    // stood for would survive and sum into the sink beside the ghost's output.
                    // One doomed cable per (upstream leg -> destination jack): a dual upstream summed
                    // into a collapsed mono input contributes TWO, and both have to go or the
                    // survivor keeps summing in beside the ghost.
                    for (int d : uniqueDsts) {
                        for (int fromJack : upstreamJacksForDst[d]) {
                            SmartSuggestion::InsertLink doomed;
                            doomed.fromJack = fromJack;
                            doomed.toJack = d;
                            doomed.p1 = upstreamJackPoint(fromJack);
                            doomed.p2 = jackPoint(/*fromGhost=*/false, d, true, false);
                            plan.doomedLinks.push_back(doomed);
                        }
                    }

                    // Upstream -> ghost. Redundant only when a cable adds NOTHING on EITHER side —
                    // no new raw ghost-input channel AND no new raw upstream-output channel. The
                    // same both-sides rule the destination-side dedupe uses, and for the same
                    // reason: a collapsed jack's fan already claims both raws on both ends, so its
                    // redundant partner contributes neither, while two DISTINCT upstream legs
                    // summing into one collapsed ghost input each contribute a real source channel
                    // and must both survive. Keying on the ghost input alone dropped the Right leg.
                    //
                    // The index mapping does the rest: a dual ghost's legs are taken in order (L->L,
                    // R->R) and a collapsed ghost clamps to its single jack, which sums.
                    std::set<int> claimedRawGhostIns, claimedRawUpstreamOuts;
                    for (size_t i = 0; i < plan.doomedLinks.size(); ++i) {
                        const int ghostInJack = ghostInLegs[std::min(i, ghostInLegs.size() - 1)];
                        const auto fan = GraphEditor::resolvePolyLink(upstreamMb, plan.doomedLinks[i].fromJack, ghostMb,
                                                                      ghostInJack);
                        bool addsGhostIn = false, addsUpstreamOut = false;
                        for (int v = 0; v < fan.voiceCount; ++v) {
                            if (claimedRawGhostIns.insert(fan.destRawChannel + v).second)
                                addsGhostIn = true;
                            if (claimedRawUpstreamOuts.insert(fan.sourceRawChannel + v * fan.sourceStride).second)
                                addsUpstreamOut = true;
                        }
                        if (!addsGhostIn && !addsUpstreamOut)
                            continue;

                        SmartSuggestion::InsertLink cable;
                        cable.fromJack = plan.doomedLinks[i].fromJack;
                        cable.toJack = ghostInJack;
                        cable.p1 = plan.doomedLinks[i].p1;
                        cable.p2 = jackPoint(/*fromGhost=*/true, ghostInJack, true, false);
                        plan.upstreamCables.push_back(cable);

                        // ONE connectPorts call here can still draw two cables (a collapsed upstream
                        // jack landing on a Dual I/O ghost covers both its legs), so the preview is
                        // resolved from the fan rather than from the cable list.
                        for (auto& leg : resolveDrawnLegs(
                                 upstreamComp->getModule(), cable.fromJack, ghostProc, ghostInJack,
                                 [&](int jack, bool) { return upstreamJackPoint(jack); },
                                 [&](int jack, bool isInput) { return jackPoint(true, jack, isInput, false); }))
                            plan.upstreamPreviewLegs.push_back(leg);
                    }
                    if (plan.upstreamCables.empty())
                        return;

                    insertPlan = std::move(plan);
                }
            }

            // One surviving pair per distinct set of raw destination channels. A collapsed jack
            // already fans across the whole raw pair, so when the destination fronts two legs (the
            // terminal sink is the only node that does — it has no ModuleBase to group them) a
            // second pair for its right leg would wire the source's LEFT leg there too, summing.
            // A no-op wherever the pairs already claim distinct raws. Insert plans are built above
            // and deliberately unaffected: a doomed link must not vanish with the pair that named it.
            //
            // A pair is redundant only when it adds NOTHING on EITHER side — no new destination raw
            // channel AND no new source raw channel. Keying on the destination alone was wrong for a
            // dedicated mono input: a dual upstream feeding a Ring Modulator's Carrier produces
            // (Left -> Carrier) and (Right -> Carrier), which share a destination raw but carry
            // DIFFERENT source legs, and summing both into that one jack is exactly the intent (it
            // is what hand-wiring and the Dual I/O toggle rewire both do). Dropping the second one
            // silently threw away a channel. The source test is what keeps the original case fixed:
            // a collapsed jack's fan already claims both raws on both sides, so its redundant
            // partner still contributes neither.
            {
                std::vector<std::pair<int, int>> keptPairs;
                std::set<int> claimedRawDsts, claimedRawSrcs;
                for (const auto& pr : pairs) {
                    const auto fan = GraphEditor::resolvePolyLink(srcMb, pr.first, dstMb, pr.second);
                    bool addsDst = false, addsSrc = false;
                    for (int v = 0; v < fan.voiceCount; ++v) {
                        if (claimedRawDsts.insert(fan.destRawChannel + v).second)
                            addsDst = true;
                        if (claimedRawSrcs.insert(fan.sourceRawChannel + v * fan.sourceStride).second)
                            addsSrc = true;
                    }
                    if (addsDst || addsSrc)
                        keptPairs.push_back(pr);
                }
                if (keptPairs.empty())
                    return;
                pairs = std::move(keptPairs);
            }

            if (requireSourceFree && srcNodeIdForFreeCheck.uid != 0) {
                std::set<int> uniqueSrcs;
                for (const auto& pr : pairs)
                    uniqueSrcs.insert(pr.first);
                for (int s : uniqueSrcs) {
                    if (!isOutputJackFree(srcNodeIdForFreeCheck, s, false))
                        return;
                }
            }

            for (const auto& [srcJack, dstJack] : pairs) {
                const int pairScore = scoreSmartPair(srcMb, srcJack, dstMb, dstJack);
                if (pairScore < 0)
                    continue;

                SmartSuggestion s;
                s.ghostIsSource = ghostIsSource;
                s.neighborId = neighbor->getNodeId();
                s.ghostJack = ghostIsSource ? srcJack : dstJack;
                s.neighborJack = ghostIsSource ? dstJack : srcJack;
                s.isMidi = false;
                const auto srcPt = jackPoint(ghostIsSource, srcJack, false, false);
                const auto dstPt = jackPoint(!ghostIsSource, dstJack, true, false);
                s.p1 = srcPt;
                s.p2 = dstPt;
                s.signal = signalForRoles(false, primaryRoleForJack(srcMb, srcJack, false));
                if (auto* smb = dynamic_cast<ModuleBase*>(srcProc))
                    s.sourceCategory = synth::ui::categoryFor(smb->getModuleType());

                // What the drop will really wire — one preview segment per DRAWN cable, which for a
                // collapsed jack landing on the terminal sink is two, not one.
                const JackPointFn ghostPointFn = [&](int jack, bool isInput) {
                    return jackPoint(/*fromGhost=*/true, jack, isInput, false);
                };
                const JackPointFn neighborPointFn = [&](int jack, bool isInput) {
                    return jackPoint(/*fromGhost=*/false, jack, isInput, false);
                };
                s.mainPreviewLegs =
                    resolveDrawnLegs(srcProc, srcJack, dstProc, dstJack, ghostIsSource ? ghostPointFn : neighborPointFn,
                                     ghostIsSource ? neighborPointFn : ghostPointFn);

                int score = pairScore;
                if (srcLegs.size() >= 2 && dstLegs.size() >= 2 && srcJack == srcLegs[0] && dstJack == dstLegs[0])
                    score += 2;

                // An insert scores like the plain cable it replaces, so it competes with (and can
                // lose to) a neighbour offering a free jack instead of always winning by novelty.
                if (insertPlan.has_value()) {
                    s.isInsert = true;
                    s.upstreamId = insertPlan->upstreamId;
                    s.doomedLinks = insertPlan->doomedLinks;
                    s.upstreamCables = insertPlan->upstreamCables;
                    s.upstreamPreviewLegs = insertPlan->upstreamPreviewLegs;
                    s.upstreamCategory = insertPlan->upstreamCategory;
                }

                audioCandidates.push_back({s, score, srcPt.getDistanceFrom(dstPt), false});
            }
        };

        // Ghost outputs -> neighbor inputs, then neighbor outputs -> ghost inputs.
        pushAudioGroup(true, ghostProc, neighborProc, drag.selfId, neighbor->getNodeId(), true);
        {
            const auto ghostDstId = drag.selfId.uid != 0 ? drag.selfId : juce::AudioProcessorGraph::NodeID{};
            pushAudioGroup(false, neighborProc, ghostProc, neighbor->getNodeId(), ghostDstId, drag.selfId.uid != 0);
        }

        // MIDI
        auto considerMidi = [&](bool ghostIsSource) {
            const juce::String ghostName = ghostProc->getName();
            const juce::String neighborName = neighborProc->getName();
            if (ghostIsSource) {
                if (!ghostProducesMidi || !neighborProc->acceptsMidi())
                    return;
                if (!isKnownMidiSourceName(ghostName) || !isKnownMidiDestName(neighborName))
                    return;
                if (!isInputJackFree(neighbor->getNodeId(), 0, true))
                    return;
                if (drag.selfId.uid != 0 && areJacksAlreadyConnected(drag.selfId, 0, neighbor->getNodeId(), 0, true))
                    return;
            } else {
                if (!neighborProc->producesMidi() || !ghostAcceptsMidi)
                    return;
                if (!isKnownMidiSourceName(neighborName) || !isKnownMidiDestName(ghostName))
                    return;
                if (drag.selfId.uid != 0 && !isInputJackFree(drag.selfId, 0, true))
                    return;
                if (drag.selfId.uid != 0 && areJacksAlreadyConnected(neighbor->getNodeId(), 0, drag.selfId, 0, true))
                    return;
            }

            const auto srcPt = jackPoint(ghostIsSource, 0, false, true);
            const auto dstPt = jackPoint(!ghostIsSource, 0, true, true);
            if (srcPt.x > dstPt.x + 8.0f)
                return;
            const float jackDist = srcPt.getDistanceFrom(dstPt);
            if (jackDist > kSmartConnectionProximityPx)
                return;

            if (requireSourceFree) {
                const auto srcId = ghostIsSource ? drag.selfId : neighbor->getNodeId();
                if (srcId.uid != 0 && !isOutputJackFree(srcId, 0, true))
                    return;
            }

            int score = 4;

            SmartSuggestion s;
            s.ghostIsSource = ghostIsSource;
            s.neighborId = neighbor->getNodeId();
            s.ghostJack = 0;
            s.neighborJack = 0;
            s.isMidi = true;
            s.p1 = srcPt;
            s.p2 = dstPt;
            s.signal = synth::ui::CableSignal::Midi;
            if (auto* smb = dynamic_cast<ModuleBase*>(ghostIsSource ? ghostProc : neighborProc))
                s.sourceCategory = synth::ui::categoryFor(smb->getModuleType());

            midiCandidates.push_back({s, score, jackDist, true});
        };
        considerMidi(true);
        considerMidi(false);
    }

    auto pickBest = [](std::vector<Candidate>& list) -> std::optional<SmartSuggestion> {
        if (list.empty())
            return std::nullopt;
        std::sort(list.begin(), list.end(), [](const Candidate& a, const Candidate& b) {
            if (a.score != b.score)
                return a.score > b.score;
            return a.distance < b.distance;
        });
        return list.front().suggestion;
    };

    // Audio: keep every suggestion that shares the winning neighbor + direction (stereo L/R
    // pairs and mono<->stereo fans are multiple candidates with the same neighborId/ghostIsSource).
    if (!audioCandidates.empty()) {
        // Only ONE neighbour's group survives this sort, so the ordering decides which offer the user
        // gets. Two competing pressures, and getting either wrong is a bug we have already shipped:
        //
        //  * A ghost being spliced into a cable sits between two cards that are BOTH valid
        //    neighbours — the upstream it is being inserted AFTER also offers a perfectly good plain
        //    "feed the new module" cable. That plain offer was winning on proximity and discarding
        //    the insert, which is what made Ctrl+drag near the Audio Output look flaky (whether the
        //    sink or the upstream won flipped with small cursor moves, so nudging down "fixed" it).
        //    It surfaced at the sink specifically because a bare AudioGraphIOProcessor scores a flat
        //    2 in scoreSmartPair, so it loses on SCORE to any real module before distance matters.
        //
        //  * But making an insert beat EVERY plain candidate outright was an overcorrection: with
        //    Ctrl held, an insert into some occupied module across the canvas then stole the drop
        //    from the free module the user was actually aiming at, so Ctrl+drag stopped connecting
        //    anything ordinary.
        //
        // So the demotion is targeted rather than global: a plain candidate loses only when its
        // neighbour is the very upstream an insert wants to reroute. Every other plain candidate
        // still competes with the insert on score and distance, so aim wins.
        std::set<juce::uint32> insertUpstreamUids;
        for (const auto& c : audioCandidates)
            if (c.suggestion.isInsert)
                insertUpstreamUids.insert(c.suggestion.upstreamId.uid);

        const auto rankOf = [&insertUpstreamUids](const Candidate& c) {
            if (c.suggestion.isInsert)
                return 1;
            // A plain offer FROM the cable's upstream is the one thing that must never mask the
            // insert: it is the same gesture read two ways, and the insert is the explicit one.
            return insertUpstreamUids.count(c.suggestion.neighborId.uid) > 0 ? 0 : 1;
        };

        std::sort(audioCandidates.begin(), audioCandidates.end(), [&rankOf](const Candidate& a, const Candidate& b) {
            const int ra = rankOf(a), rb = rankOf(b);
            if (ra != rb)
                return ra > rb;
            if (a.score != b.score)
                return a.score > b.score;
            return a.distance < b.distance;
        });
        const auto& best = audioCandidates.front();
        for (const auto& c : audioCandidates) {
            if (c.suggestion.neighborId != best.suggestion.neighborId)
                continue;
            if (c.suggestion.ghostIsSource != best.suggestion.ghostIsSource)
                continue;
            if (c.suggestion.isInsert != best.suggestion.isInsert)
                continue; // never mix a reroute and a plain add in one applied group
            smartSuggestions_.push_back(c.suggestion);
        }
    }
    if (auto best = pickBest(midiCandidates))
        smartSuggestions_.push_back(*best);

    if (smartSuggestions_ != previous)
        host_.repaintCanvas();
}

void SmartConnectionEngine::applySmartSuggestions(juce::AudioProcessorGraph::NodeID ghostNodeId, bool recordUndo) {
    if (smartSuggestions_.empty() || ghostNodeId.uid == 0)
        return;

    auto applyAll = [this, ghostNodeId] {
        for (const auto& s : smartSuggestions_) {
            if (s.isInsert) {
                // Reroute, never double: drop EVERY doomed cable first so the sink's jacks are free
                // for the ghost's output, then wire upstream -> ghost -> sink. Dropping only this
                // leg's cable would leave the other one summing into the sink beside the ghost.
                // Both sets are group-wide and deduped, so a second insert suggestion re-running
                // them is a no-op. All of it shares the caller's transaction — one undo, one step.
                for (const auto& doomed : s.doomedLinks)
                    disconnectAudioLink(s.upstreamId, doomed.fromJack, s.neighborId, doomed.toJack);
                for (const auto& cable : s.upstreamCables)
                    host_.connectPorts(s.upstreamId, cable.fromJack, ghostNodeId, cable.toJack, false, false);
                host_.connectPorts(ghostNodeId, s.ghostJack, s.neighborId, s.neighborJack, false, false);
                continue;
            }

            const auto srcId = s.ghostIsSource ? ghostNodeId : s.neighborId;
            const auto dstId = s.ghostIsSource ? s.neighborId : ghostNodeId;
            const int srcJack = s.ghostIsSource ? s.ghostJack : s.neighborJack;
            const int dstJack = s.ghostIsSource ? s.neighborJack : s.ghostJack;
            host_.connectPorts(srcId, srcJack, dstId, dstJack, s.isMidi, false);
        }
    };

    if (recordUndo && host_.undo())
        host_.undo()->recordStructuralChange(host_.graph(), applyAll);
    else
        applyAll();

    clearSmartSuggestions();
}
