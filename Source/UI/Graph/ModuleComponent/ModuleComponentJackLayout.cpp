// ModuleComponentJackLayout.cpp
//
// Where a card's input jacks are: which visible jacks draw a gutter row, which land on a knob, and the
// JackLayoutPass that lets one cable rebuild work each of those out once per card.

#include "ModuleComponent.h"
#include "ModuleComponentJackMemo.h"
#include "Modules/ModuleBase.h"
#include "UI/Graph/GraphEditor/GraphEditorPaintMemo.h"
#include <algorithm>

namespace {
juce::uint32 activeJackLayoutPass = 0; // 0: no pass open; message thread only
juce::uint32 lastJackLayoutPass = 0;

// The card's memo for the open pass, or null when none is open.
ModuleComponentJackMemo* passMemo(std::unique_ptr<ModuleComponentJackMemo>& memo) {
    if (activeJackLayoutPass == 0)
        return nullptr;
    if (memo == nullptr)
        memo = std::make_unique<ModuleComponentJackMemo>();
    memo->beginPass(activeJackLayoutPass);
    return memo.get();
}
} // namespace

const std::vector<ModulationTarget>&
ModuleComponent::modulationTargetsForPass(std::vector<ModulationTarget>& fresh) const {
    auto* mb = dynamic_cast<ModuleBase*>(module);
    auto* memo = passMemo(jackMemo_);
    if (memo != nullptr && memo->targetsKnown)
        return memo->targets;
    auto& out = memo != nullptr ? memo->targets : fresh;
    out = mb != nullptr ? mb->getModulationTargets() : std::vector<ModulationTarget>{};
    ++graph_editor_paint::workCounters().modTargetReads;
    if (memo != nullptr)
        memo->targetsKnown = true;
    return out;
}

// True when visible input jack `index` is a ModulationTarget whose knob resolves on this
// card RIGHT NOW -- recomputed live off getModulationTargets()/mapInputChannel()/
// sliderIndexForModTarget() every call outside a JackLayoutPass (poly toggle and Dual I/O change what resolves; a
// tabbed knob with a gutter jack never binds it, knobAnchorForVisibleInputJack), never cached across a layout. A module
// with no ModulationTarget mapping to `index` (an audio/pitch/gate/MIDI jack, or a CV jack with no bound knob, e.g.
// Oscillator's Pitch CV) is never knob-bound, matching the plain gutter behaviour exactly.
bool ModuleComponent::isInputJackKnobBound(int index) const { return knobAnchorForVisibleInputJack(index).has_value(); }

// Shared by isInputJackKnobBound (the "is it hidden" question) and getPortCenter's redirect
// branch (the "where does it land" question) so the two can never resolve a different target for
// the same visible index -- see the declaration's comment (ModuleComponent.h).
std::optional<juce::Point<float>> ModuleComponent::knobAnchorForVisibleInputJack(int index) const {
    auto* mb = dynamic_cast<ModuleBase*>(module);
    if (mb == nullptr)
        return std::nullopt;
    auto* memo = passMemo(jackMemo_);
    if (memo != nullptr && index >= 0 && (size_t)index < memo->anchors.size() &&
        memo->anchors[(size_t)index].has_value())
        return *memo->anchors[(size_t)index];
    const auto anchor = [&]() -> std::optional<juce::Point<float>> {
        std::vector<ModulationTarget> fresh;
        for (const auto& target : modulationTargetsForPass(fresh)) {
            if (mb->mapInputChannel(target.channelIndex).visibleJackIndex != index)
                continue;
            if (!target.knobOnly && tabbedKnobFor(target) != nullptr)
                return std::nullopt; // a tabbed knob's jack keeps its gutter row; the re-anchor pass moves its cable
            return getModTargetKnobAnchor(target.channelIndex);
        }
        return std::nullopt;
    }();
    if (memo != nullptr && index >= 0) {
        if ((size_t)index >= memo->anchors.size())
            memo->anchors.resize((size_t)index + 1);
        memo->anchors[(size_t)index] = anchor;
    }
    return anchor;
}

// A cable rebuild asks every card for the position of each of its cable ends, and each answer re-derived the whole
// jack column, and each jack's knob, through getModulationTargets() (allocating every target's name): the rebuild cost
// grew with ports x cables and took most of a zoom frame, and most of a drag tick across a macro border, at 80 tracks.
// Within one pass nothing changes a card, so each card reads its targets once, works its column out once and finds
// each jack's knob once; the pass ends with the rebuild, so nothing is cached across one.
ModuleComponent::JackLayoutPass::JackLayoutPass() {
    jassert(activeJackLayoutPass == 0); // passes do not nest
    activeJackLayoutPass = ++lastJackLayoutPass;
}

ModuleComponent::JackLayoutPass::~JackLayoutPass() { activeJackLayoutPass = 0; }

// paint()'s input loop, getPortForPoint()'s input loop, and getInputPortColumns() all read
// THIS list rather than re-deriving "which jacks are hidden" each their own way, so they can never
// disagree about what's actually on screen. Never cached across a call outside a JackLayoutPass --
// isInputJackKnobBound recomputes live off current slider visibility every time (poly toggle, Dual I/O, the More row).
std::vector<int> ModuleComponent::drawnInputJackIndices() const {
    auto* memo = passMemo(jackMemo_);
    if (memo != nullptr && memo->drawnKnown)
        return memo->drawn;
    std::vector<int> drawn;
    if (module == nullptr)
        return drawn;
    int visible = 0;
    if (auto* mb = dynamic_cast<ModuleBase*>(module))
        visible = mb->getVisibleInputPortCount();
    else
        visible = module->getTotalNumInputChannels(); // no ModuleBase (e.g. Audio Input/Output) -- never knob-bound
    drawn.reserve(visible);
    // A knob-only target's jack (ModulationTarget::knobOnly, e.g. the Filter's Custom CV) never takes a
    // row, even while its knob is hidden: a card whose mode hides those knobs keeps its old jack column.
    std::vector<int> knobOnly;
    std::vector<ModulationTarget> fresh;
    if (auto* mb = dynamic_cast<ModuleBase*>(module))
        for (const auto& target : modulationTargetsForPass(fresh))
            if (target.knobOnly)
                knobOnly.push_back(mb->mapInputChannel(target.channelIndex).visibleJackIndex);
    for (int i = 0; i < visible; ++i)
        if (!isInputJackKnobBound(i) && std::find(knobOnly.begin(), knobOnly.end(), i) == knobOnly.end())
            drawn.push_back(i);
    if (memo != nullptr) {
        memo->drawn = drawn;
        memo->drawnKnown = true;
    }
    return drawn;
}
