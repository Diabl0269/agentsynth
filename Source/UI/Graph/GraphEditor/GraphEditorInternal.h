// GraphEditorInternal.h
//
// Private helpers shared by two or more GraphEditor*.cpp translation units. GraphEditor
// itself is declared in GraphEditor.h; its implementation is split across sibling
// GraphEditor*.cpp files in this directory. Not registered as a CMake source -- header only.
// `inline` (not `static`) so a constant/function pulled in by several .cpp files still
// resolves to one definition, matching the single-TU behaviour before the split.
#pragma once

#include "GraphEditor.h"
#include "UI/Layout/UIAnimation.h"
#include <algorithm>
#include <cmath>

namespace detail {

inline float edgeToEdgeDistance(juce::Rectangle<float> a, juce::Rectangle<float> b) {
    if (a.intersects(b))
        return 0.0f;
    float dx = 0.0f;
    if (a.getRight() < b.getX())
        dx = b.getX() - a.getRight();
    else if (b.getRight() < a.getX())
        dx = a.getX() - b.getRight();
    float dy = 0.0f;
    if (a.getBottom() < b.getY())
        dy = b.getY() - a.getBottom();
    else if (b.getBottom() < a.getY())
        dy = a.getY() - b.getBottom();
    return std::sqrt(dx * dx + dy * dy);
}

// Category of the module a cable leaves from. Unknown/!ModuleBase nodes fall back to Utility so
// BySourceCategory mode always has a colour to use.
inline synth::ui::ModuleCategory categoryForNode(juce::AudioProcessorGraph::Node* node) {
    if (node != nullptr)
        if (auto* mb = dynamic_cast<ModuleBase*>(node->getProcessor()))
            return synth::ui::categoryFor(mb->getModuleType());
    return synth::ui::ModuleCategory::Utility;
}

// ---- Card jacks (docs/macros/ports.md#cable-rendering-across-the-boundary) ------------------
// The collapsed card's minimum height — deliberately independent of however large or scattered
// the group it stands in for is; that is the whole point of collapsing. The card grows past it only
// to fit its port rows (macroCardHeightFor). Its width matches a standard module card's so it sits
// comfortably on the same grid.
inline constexpr int kMacroCardHeight = 90;
// Same inset from the card's left/right edge ModuleComponent's own MIDI jacks use on an
// identically-wide kSingleWidth card (x=10 / getWidth()-10) — a macro's boundary jacks read like
// any other module's.
inline constexpr int kMacroCardJackInsetX = 10;
// Click tolerance, matching ModuleComponent::getPortForPoint's own `< 10`.
inline constexpr float kMacroCardJackHitRadius = 10.0f;

// ---- Macro port sidebars (docs/macros/ports.md#how-a-port-is-drawn) ---------------------------
// One port per 16px row, on both the collapsed card and the open macro. A strip's width is FIXED: the
// same on both sides, reserved even with no ports, and never dependent on zoom or on the port names.
// Zoom only fades the names, the '-' button and each strip's inner divider, so every layout below is
// zoom-independent.
inline constexpr int kMacroPortRowHeight = 16;
inline constexpr int kMacroPortRowsTop = 30;           // first row's top on the card, below the title row
inline constexpr int kMacroPortStripFooter = 22;       // room below the last row for the '+'/'-' pair
inline constexpr int kMacroPortStripInset = 18;        // name x from the strip's outer edge (jack at 10, + 8)
inline constexpr int kMacroPortStripPadding = 8;       // after the name column
inline constexpr int kMacroPortNameRoom = 62;          // the name column's width; a longer name is ellipsised
inline constexpr int kMacroHullStripInnerJackRoom = 8; // open macro only: room for the inner jack
// A card strip's width (88px): on the 280px card two strips leave 104px for the title, preview and
// member count. The open macro's strip adds room for its inner jack.
inline constexpr int kMacroCardStripWidth = kMacroPortStripInset + kMacroPortNameRoom + kMacroPortStripPadding;
inline constexpr int kMacroHullStripWidth = kMacroCardStripWidth + kMacroHullStripInnerJackRoom;
// Height reserved above an open macro's members for its name chip row, and the port rows' first-row
// offset from the hull's top (the rows start 6px below the chip row).
inline constexpr int kMacroChipRowHeight = 24;
inline constexpr int kMacroPortRowsBelowChip = 6;
// Port names fade in over this zoom range (docs/layout/animation.md): fully hidden at or below Lo,
// fully drawn at or above Hi.
inline constexpr float kMacroPortNamesFadeLo = 0.5f;
inline constexpr float kMacroPortNamesFadeHi = 0.7f;
inline constexpr float kMacroPortNameFontSize = 9.5f;
/** Card height that fits `portsOnBusiestSide` rows, never below the fixed footprint. */
inline int macroCardHeightFor(int portsOnBusiestSide) {
    return juce::jmax(kMacroCardHeight,
                      kMacroPortRowsTop + portsOnBusiestSide * kMacroPortRowHeight + kMacroPortStripFooter);
}
/** Opacity of the port names, the '-' glyph and the strips' inner dividers: a pure function of zoom. */
inline float macroPortNameAlphaAtZoom(float zoom) {
    return synth::ui::easeInOutCubic(
        juce::jlimit(0.0f, 1.0f, (zoom - kMacroPortNamesFadeLo) / (kMacroPortNamesFadeHi - kMacroPortNamesFadeLo)));
}
/** The '-' button is clickable only once it is at least half faded in. */
inline bool macroPortRemoveClickableAtZoom(float zoom) { return macroPortNameAlphaAtZoom(zoom) >= 0.5f; }
/** The strip fill's opacity: recedes as the names fade out. */
inline float macroStripFillAlpha(float nameAlpha) { return 0.25f + 0.30f * nameAlpha; }
/** Zoomed out, an open macro's strip narrows to this rail along the outline and each port's interior jack slides
 *  onto its boundary jack (both driven by the one name-fade factor `t`; docs/macros/ports.md). Canvas geometry
 *  (hull, strip widths, widget bounds) never changes. */
inline constexpr float kMacroStripRailWidth = 10.0f;
/** A strip's painted width: the rail at t = 0, the full `fullWidth` at t = 1. */
inline float macroStripPaintedWidth(float fullWidth, float t) {
    return kMacroStripRailWidth + (fullWidth - kMacroStripRailWidth) * t;
}
/** The interior jack's x: on the boundary jack at t = 0, at its own `interiorX` at t = 1. */
inline float macroPortInteriorJackX(float boundaryX, float interiorX, float t) {
    return boundaryX + (interiorX - boundaryX) * t;
}
inline constexpr float kMacroPortFooterButtonSize = 8.0f;
inline constexpr float kMacroPortFooterButtonFromBottom = 12.0f; // button top, up from the strip's bottom edge
inline constexpr float kMacroPortAddButtonInset = 4.0f;          // '+' x from its strip's outer edge
inline constexpr float kMacroPortRemoveButtonInset = 16.0f;      // '-' x from its strip's outer edge
/** The glyph shared by the '+' and '-' buttons on the card and on the open macro's hull: a ring, and one
 *  horizontal stroke ('-') or a horizontal and a vertical one ('+'). */
inline void paintMacroPortFooterButton(juce::Graphics& g, juce::Rectangle<float> b, juce::Colour colour, bool isAdd) {
    g.setColour(colour);
    g.drawEllipse(b, 1.2f);
    const auto cross = b.reduced(b.getWidth() * 0.28f);
    if (isAdd)
        g.drawLine(cross.getCentreX(), cross.getY(), cross.getCentreX(), cross.getBottom(), 1.4f);
    g.drawLine(cross.getX(), cross.getCentreY(), cross.getRight(), cross.getCentreY(), 1.4f);
}
/** Paints both sidebar strips and their '+'/'-' buttons for every expanded macro, under the port widgets
 *  (GraphEditorMacroHullStrips.cpp). `zoom` is the content component's scale, deciding whether '-' shows. */
void paintMacroPortStrips(juce::Graphics& g, GraphEditor& editor, float zoom);
/** Every macro as the canvas draws it now, for the border that leaves or returns with a delete or its undo
 *  (CardGlideAnimator::Border; GraphEditorMacroHullStrips.cpp). `zoom` is the content component's scale. */
std::vector<CardGlideAnimator::Border> captureMacroBorders(GraphEditor& editor, float zoom);

/** True when a source channel carries a structural, absolute-valued signal rather than normalised
 *  modulation. Poly MIDI's pitch fan is raw Hz and its gate fan is a 0/1 trigger; neither should ever
 *  be routed through an attenuverter, which would scale an absolute frequency and feed Hz-magnitude
 *  peaks into the UI's signal-activity metering. */
inline bool carriesStructuralSignal(const ModuleBase* source, int sourceRawChannel) {
    if (source == nullptr)
        return false;
    const PortRole role = source->mapOutputChannel(sourceRawChannel).role;
    return role == PortRole::Pitch || role == PortRole::Gate;
}

/** Ranks a candidate source/destination fan pairing; higher wins. Matching roles are the strongest
 *  signal (this is what tells Poly MIDI's Pitch fan from its Gate fan when both share one jack); a
 *  ModCV or unclassified end is a wildcard, since mod inputs accept anything; equal fan widths break
 *  what is left. */
inline int scoreJackPair(const ModuleBase::JackTarget& src, const ModuleBase::JackTarget& dst) {
    int score = 0;
    if (src.role == dst.role)
        score += 4;
    else if (src.role == PortRole::ModCV || dst.role == PortRole::ModCV || src.role == PortRole::Other ||
             dst.role == PortRole::Other)
        score += 1;

    if (src.voiceSpan == dst.voiceSpan && src.voiceSpan > 1)
        score += 2;

    return score;
}

inline PortRole primaryRoleForJack(const ModuleBase* mb, int visibleJack, bool isInput) {
    if (mb == nullptr)
        return PortRole::Other;
    const auto targets = mb->getJackTargets(visibleJack, isInput);
    if (targets.empty())
        return PortRole::Other;
    return targets.front().role;
}

inline synth::ui::CableSignal signalForRoles(bool isMidi, PortRole srcRole) {
    if (isMidi)
        return synth::ui::CableSignal::Midi;
    switch (srcRole) {
    case PortRole::Pitch:
        return synth::ui::CableSignal::Pitch;
    case PortRole::Gate:
        return synth::ui::CableSignal::Gate;
    case PortRole::ModCV:
        return synth::ui::CableSignal::ModCV;
    default:
        return synth::ui::CableSignal::Audio;
    }
}

inline int scoreSmartPair(const ModuleBase* srcMb, int srcJack, const ModuleBase* dstMb, int dstJack) {
    if (srcMb == nullptr || dstMb == nullptr) {
        // Audio I/O nodes without ModuleBase: treat as plain mono audio jacks.
        return 2;
    }
    int best = -1;
    for (const auto& s : srcMb->getJackTargets(srcJack, false)) {
        for (const auto& d : dstMb->getJackTargets(dstJack, true)) {
            best = std::max(best, scoreJackPair(s, d));
        }
    }
    return best;
}

/** ModuleBase defaults producesMidi/acceptsMidi to true, so almost every card reports MIDI
 *  jacks. Smart-connect only suggests MIDI for modules that actually source or sink MIDI in
 *  practice (mirrors the AI merge auto-connect allow-lists). */
inline bool isKnownMidiSourceName(const juce::String& name) {
    return name == "Sequencer" || name == "Poly Sequencer" || name == "Poly MIDI" || name == "MIDI Keyboard" ||
           name == "External MIDI";
}

inline bool isKnownMidiDestName(const juce::String& name) {
    return name == "Oscillator" || name == "Sampler" || name == "Drum Kit" || name == "Wavetable" || name == "ADSR" ||
           name == "Sequencer" || name == "Poly Sequencer" || name == "Poly MIDI";
}

inline bool isStereoLegLabel(const juce::String& label) {
    const auto l = label.trim().toLowerCase();
    return l == "left" || l == "right" || l == "audio l" || l == "audio r" || l == "l" || l == "r";
}

inline bool isLeftLegLabel(const juce::String& label) {
    const auto l = label.trim().toLowerCase();
    return l == "left" || l == "audio l" || l == "l";
}

inline bool audioJackIsModCvDest(const ModuleBase* dest, int visibleJack) {
    if (dest == nullptr)
        return false;
    const auto targets = dest->getJackTargets(visibleJack, true);
    for (const auto& t : targets) {
        for (const auto& mt : dest->getModulationTargets()) {
            if (mt.channelIndex == t.rawHeadChannel)
                return true;
        }
    }
    return false;
}

/** True for the graph's terminal audio sink. Audio Output is a bare juce::AudioGraphIOProcessor —
 *  never a ModuleBase — because the graph's output channel count is tied to that node. Detected by
 *  type rather than by the "Audio Output" name isSingletonIOModule matches on, so a ModuleBase that
 *  happened to be called that could not impersonate the sink. */
inline bool isTerminalAudioSink(const juce::AudioProcessor* proc) {
    using IOProcessor = juce::AudioProcessorGraph::AudioGraphIOProcessor;
    if (auto* io = dynamic_cast<const IOProcessor*>(proc))
        return io->getType() == IOProcessor::audioOutputNode;
    return false;
}

/** Visible audio jacks used for smart-connect. A stereo pair is returned only when jacks are
 *  explicitly labeled Left/Right (or Audio L/R) — arity alone is not enough (Math A/B would
 *  otherwise look like stereo). Unlabeled audio-ish jacks contribute at most one mono leg so
 *  Voice Mixer banks are not fan-wired. Audio I/O nodes without ModuleBase use channel count. */
inline std::vector<int> collectSmartAudioLegs(juce::AudioProcessor* proc, bool isInput) {
    std::vector<int> legs;
    if (proc == nullptr)
        return legs;

    auto* mb = dynamic_cast<ModuleBase*>(proc);
    if (mb == nullptr) {
        const int n = isInput ? proc->getTotalNumInputChannels() : proc->getTotalNumOutputChannels();
        if (n >= 2)
            return {0, 1};
        if (n == 1)
            return {0};
        return legs;
    }

    const int vis = isInput ? mb->getVisibleInputPortCount() : mb->getVisibleOutputPortCount();
    std::vector<int> labeled;
    std::vector<int> unlabeled;

    for (int j = 0; j < vis; ++j) {
        if (isInput && audioJackIsModCvDest(mb, j))
            continue;
        const PortRole role = primaryRoleForJack(mb, j, isInput);
        // A Sidechain (Key) jack is never a smart-connect audio leg: keying is always a deliberate cable.
        if (role == PortRole::Pitch || role == PortRole::Gate || role == PortRole::Midi || role == PortRole::ModCV ||
            role == PortRole::Sidechain)
            continue;

        const juce::String label = isInput ? mb->getInputPortLabel(j) : mb->getOutputPortLabel(j);
        if (isStereoLegLabel(label))
            labeled.push_back(j);
        else if (role == PortRole::Audio || role == PortRole::Other)
            unlabeled.push_back(j);
    }

    if (labeled.size() >= 2) {
        std::sort(labeled.begin(), labeled.end(), [&](int a, int b) {
            const auto la = isInput ? mb->getInputPortLabel(a) : mb->getOutputPortLabel(a);
            const auto lb = isInput ? mb->getInputPortLabel(b) : mb->getOutputPortLabel(b);
            const bool aLeft = isLeftLegLabel(la);
            const bool bLeft = isLeftLegLabel(lb);
            if (aLeft != bLeft)
                return aLeft;
            return a < b;
        });
        return {labeled[0], labeled[1]};
    }
    if (labeled.size() == 1)
        return labeled;

    // Unlabeled: mono only — never treat Math A/B (or any two Others) as L/R.
    if (!unlabeled.empty())
        return {unlabeled.front()};
    return legs;
}

/** Jack pairs for a smart audio link: L→L/R→R, or fan mono↔stereo both ways. */
inline std::vector<std::pair<int, int>> expandAudioJackPairs(const std::vector<int>& srcLegs,
                                                             const std::vector<int>& dstLegs) {
    std::vector<std::pair<int, int>> pairs;
    if (srcLegs.empty() || dstLegs.empty())
        return pairs;

    if (srcLegs.size() >= 2 && dstLegs.size() >= 2) {
        pairs.emplace_back(srcLegs[0], dstLegs[0]);
        pairs.emplace_back(srcLegs[1], dstLegs[1]);
    } else if (srcLegs.size() == 1 && dstLegs.size() >= 2) {
        pairs.emplace_back(srcLegs[0], dstLegs[0]);
        pairs.emplace_back(srcLegs[0], dstLegs[1]);
    } else if (srcLegs.size() >= 2 && dstLegs.size() == 1) {
        pairs.emplace_back(srcLegs[0], dstLegs[0]);
        pairs.emplace_back(srcLegs[1], dstLegs[0]);
    } else {
        pairs.emplace_back(srcLegs[0], dstLegs[0]);
    }
    return pairs;
}

} // namespace detail

// Delete heal (GraphEditorDeleteHeal.cpp): one healable audio splice, captured BEFORE
// `deletedIds` is removed from the graph. `upstreamId`/`downstreamId` are both SURVIVORS (never a
// member of `deletedIds`), found by walking off each end of a deleted run of exactly-one-audio-
// in/-out nodes. Applying it wires upstreamId's `upstreamJack` to downstreamId's `downstreamJack`
// the same way a user-drawn cable would (resolvePolyLink's L->L/R->R mapping), re-validated with
// graph.isAnInputTo()/canConnect() once the deletion has actually happened.
// `upstreamDualPair`/`downstreamDualPair`: true when that side is one half of a genuine Dual-I/O
// L/R pair on BOTH the splice's own endpoint and its peer (mergeDualIoStereoPairLegs' own comment
// in GraphEditorDeleteHeal.cpp) -- healDeletedChain then also heals jack+1 explicitly on that side,
// since resolvePolyLink has no notion of "two separate jacks" within one PolyLink.
struct GraphEditor::HealSplice {
    juce::AudioProcessorGraph::NodeID upstreamId, downstreamId;
    int upstreamJack = 0, downstreamJack = 0;
    bool upstreamDualPair = false, downstreamDualPair = false;
    bool operator==(const HealSplice& o) const noexcept {
        return upstreamId == o.upstreamId && downstreamId == o.downstreamId && upstreamJack == o.upstreamJack &&
               downstreamJack == o.downstreamJack && upstreamDualPair == o.upstreamDualPair &&
               downstreamDualPair == o.downstreamDualPair;
    }
};
