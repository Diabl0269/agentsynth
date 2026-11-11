// ModuleComponentCableParking.cpp -- where a modulation cable ends while the knob it lands on is out of view: a
// knob-only target folded into the More row ends at the More button, one on an unselected tab at the tab strip, one
// on an unpicked Drum Kit pad at the top of the pad panel, each at the card's left edge; a knob whose jack has a gutter
// row of its own goes back to that row. A tab or pad pick glides the ends (CableEndGlide), as a More toggle does.
// docs/layout/cables.md#knob-landing.

#include "ModuleComponent.h"
#include "Modules/ModuleBase.h"
#include "UI/Graph/CardBody/CardBody.h"
#include "UI/Graph/GraphEditor/CableEndGlide.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/GraphEditor/GraphEditorPaintMemo.h"

namespace {
constexpr float kParkedEndX = 10.0f;   // the gutter's x: where a jack would be
constexpr float kParkedEndStep = 6.0f; // between two parked ends on one card, so their cables never meet
} // namespace

// A knob-only target draws no jack, so while its knob is folded into the More row a cable has no row to end on: the
// gutter slot its rank would give it belongs to the next real jack (the Key input, a second Audio row).
const juce::Component* ModuleComponent::moreRowButtonForKnobOnlyJack(int index) const {
    auto* mb = dynamic_cast<ModuleBase*>(module);
    if (mb == nullptr)
        return nullptr;
    for (const auto& target : mb->getModulationTargets()) {
        if (!target.knobOnly || mb->mapInputChannel(target.channelIndex).visibleJackIndex != index)
            continue;
        const int si = sliderIndexForModTarget(target);
        if (si >= 0 && sliders[si]->isVisible() && !(cardBody_ != nullptr && cardBody_->isFoldingAway(*sliders[si])))
            return nullptr;
        auto* button = findChildWithID("cardMoreRow");
        return button != nullptr && button->isVisible() ? button : nullptr;
    }
    return nullptr;
}

// A cable landing on a knob whose jack has a gutter row of its own (not knob-only): where that row is once the knob
// no longer shows, i.e. the drawn column with this jack added back. Null when the cable lands on no such knob.
std::optional<juce::Point<float>> ModuleComponent::foldedJackCentre(int destChannel) const {
    auto* mb = dynamic_cast<ModuleBase*>(module);
    if (mb == nullptr)
        return std::nullopt;
    for (const auto& target : mb->getModulationTargets()) {
        if (target.channelIndex != destChannel)
            continue;
        const int visible = mb->getVisibleInputPortCount();
        const int jack = mb->mapInputChannel(destChannel).visibleJackIndex;
        if (target.knobOnly || jack < 0 || jack >= visible || !isInputJackKnobBound(jack))
            return std::nullopt;
        auto drawn = drawnInputJackIndices();
        drawn.insert(std::lower_bound(drawn.begin(), drawn.end(), jack), jack);
        return gutterInputJackCentre(drawn, jack).toFloat();
    }
    return std::nullopt;
}

// The knob `target` binds, shown or not, when it sits in a tab section; null for any other target. Only a bound
// parameter is looked up (every tabbed knob has one).
const juce::Slider* ModuleComponent::tabbedKnobFor(const ModulationTarget& target) const {
    const auto* mb = dynamic_cast<const ModuleBase*>(module);
    const auto* param = mb != nullptr && cardBody_ != nullptr ? mb->parameterForModTarget(target) : nullptr;
    const int si = param != nullptr ? sliderParams.indexOf(const_cast<juce::RangedAudioParameter*>(param)) : -1;
    return si >= 0 && cardBody_->isTabbed(*sliders[si]) ? sliders[si] : nullptr;
}

// A knob-only target has no jack to fall back on, so while its knob is out of view its cable parks at the card's
// left edge beside what brings the knob back: the tab strip for a knob on another tab, the top of the pad panel for
// a pad knob of a pad not picked. The re-anchor pass (GraphEditor::reanchorCablesToKnobTargets) ranks the parked ends
// of one card, and each rank sits one step lower, kept inside the card, so two cables never share an end.
std::optional<juce::Point<float>> ModuleComponent::parkedCableEnd(int destChannel, int rank) const {
    auto* mb = dynamic_cast<ModuleBase*>(module);
    if (mb == nullptr)
        return std::nullopt;
    for (const auto& target : mb->getModulationTargets()) {
        if (target.channelIndex != destChannel)
            continue;
        if (!target.knobOnly || getModTargetKnobAnchor(destChannel).has_value())
            return std::nullopt;
        std::optional<int> y;
        if (const auto* knob = tabbedKnobFor(target); knob != nullptr && !knob->isVisible())
            if (const auto* strip = cardBody_->tabStripOf(*knob); strip != nullptr && strip->isVisible())
                y = strip->getBounds().getCentreY();
        if (!y.has_value())
            return std::nullopt;
        const float lowest = juce::jmax((float)*y, (float)getHeight() - kParkedEndStep);
        return juce::Point<float>{kParkedEndX, juce::jmin((float)*y + (float)rank * kParkedEndStep, lowest)};
    }
    return std::nullopt;
}

// The one start for a pick that moves cable ends at once (a tab, a Drum Kit pad): CableEndGlide records where every
// cable on this card is drawn now and glides each to wherever it lands after the pick, under the motion settings.
void ModuleComponent::glideCableEnds() {
    if (auto* memo = graph_editor_paint::CanvasMemo::of(owner))
        memo->cableEndGlide().beginMove(*this);
}
