// ModuleComponentParkedEnd.cpp -- where a cable ends while its knob-only target is out of view, and the pickup of that
// end. A knob-only target has no jack to fall back on, so its cable ends on the control that brings the knob back, as
// a knob folded into the Oscillator's More row ends at the More button: the tab button of a knob on another tab, the
// tile of a Drum Kit pad that is not picked. A tab or pad pick glides the ends (CableEndGlide). docs/layout/cables.md.

#include "ModuleComponent.h"
#include "ModuleComponentInternal.h"
#include "Modules/DrumKit/DrumKitModule.h"
#include "Modules/ModuleBase.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/DrumKitPads/DrumKitPadsSection.h"
#include "UI/Graph/ModuleComponent/SamplerChrome/DrumKitChrome.h"

namespace {
constexpr float kParkedDotGap = 1.0f;  // clear space between the dot and the button's border
constexpr float kParkedEndX = 10.0f;   // the fallback spot's x: where a gutter jack would be
constexpr float kParkedEndStep = 6.0f; // between two parked ends on one button, so their cables never meet
constexpr int kFallbackSlot = 0;       // the slot of every end that has no button of its own

// How far the dot's centre sits inside a button's or tile's edge: the dot's radius, the border drawn on that edge (a
// tab button and a pad tile both stroke the theme's hairline) and a gap, so the whole dot lies clear of the border.
float parkedInsetFor(const juce::Component& card) {
    const float border = juce::jmax(1.0f, synth::theme::themeOf(card).metrics.borderWidth);
    return ModuleComponent::kKnobLandingDotDiameter * 0.5f + border + kParkedDotGap;
}

bool visibleUpTo(const juce::Component& card, const juce::Component& control) {
    const juce::Component* c = &control;
    for (; c != nullptr && c != &card; c = c->getParentComponent())
        if (!c->isVisible())
            return false;
    return c == &card;
}

// The 0-based pad a "pad<N><Field>" parameter id names.
int padIndexOf(const juce::String& paramId) {
    return paramId.substring(3).initialSectionContainingOnly("0123456789").getIntValue() - 1;
}

// A slot per button: its place on the card, never 0 (the fallback's).
int slotFor(juce::Rectangle<int> button) { return 1 + button.getX() + button.getY() * 4096; }
} // namespace

// {button or tile (card-local; empty: the fallback spot), fallback y} of a parked target. The tab button holding a knob
// on an unselected tab, else the tile of a pad not picked; else the strip's centre or the top of the pad panel, where
// the button is not on the card (a hidden pad).
std::optional<std::pair<juce::Rectangle<int>, int>>
ModuleComponent::parkedPlaceFor(const ModulationTarget& target) const {
    if (const auto* knob = tabbedKnobFor(target); knob != nullptr && !knob->isVisible())
        return parkedTabPlaceFor(*knob);
    if (!DrumKitModule::isPadParamId(target.paramId))
        return std::nullopt;
    for (const auto& entry : midiLearnableRegistry_.entries()) {
        if (entry.hosted || !DrumKitModule::isPadParamId(entry.paramId) || entry.component == nullptr)
            continue;
        const auto* panel = entry.component->getParentComponent();
        if (panel == nullptr || panel == this)
            continue;
        if (auto* chrome = dynamic_cast<synth::ui::DrumKitChrome*>(getInstrumentChrome())) {
            auto& grid = chrome->getPads().grid();
            const int pad = padIndexOf(target.paramId);
            if (visibleUpTo(*this, grid) && grid.isPadVisible(pad))
                return std::pair{getLocalArea(&grid, grid.padBounds(pad)), 0};
        }
        return std::pair{juce::Rectangle<int>(), getLocalArea(panel, panel->getLocalBounds()).getY()};
    }
    return std::nullopt;
}

// The button or tile `destChannel`'s parked end sits on (equal for ends sharing one); -1 when it is not parked.
int ModuleComponent::parkedCableSlot(int destChannel) const {
    if (dynamic_cast<ModuleBase*>(module) == nullptr)
        return -1;
    std::vector<ModulationTarget> fresh;
    for (const auto& target : modulationTargetsForPass(fresh)) {
        if (target.channelIndex != destChannel)
            continue;
        if (!target.knobOnly || getModTargetKnobAnchor(destChannel).has_value())
            return -1;
        const auto place = parkedPlaceFor(target);
        return !place.has_value() ? -1 : place->first.isEmpty() ? kFallbackSlot : slotFor(place->first);
    }
    return -1;
}

// The first end is level with the button's middle; each rank sits one step lower, kept inside the button (or the card
// for the fallback spot).
std::optional<juce::Point<float>> ModuleComponent::parkedCableEnd(int destChannel, int rank) const {
    if (dynamic_cast<ModuleBase*>(module) == nullptr)
        return std::nullopt;
    std::vector<ModulationTarget> fresh;
    for (const auto& target : modulationTargetsForPass(fresh)) {
        if (target.channelIndex != destChannel)
            continue;
        if (!target.knobOnly || getModTargetKnobAnchor(destChannel).has_value())
            return std::nullopt;
        const auto place = parkedPlaceFor(target);
        if (!place.has_value())
            return std::nullopt;
        const float step = (float)rank * kParkedEndStep;
        if (place->first.isEmpty()) {
            const float y = (float)place->second;
            return juce::Point<float>{kParkedEndX,
                                      juce::jmin(y + step, juce::jmax(y, (float)getHeight() - kParkedEndStep))};
        }
        const auto& b = place->first;
        const float centre = (float)b.getCentreY();
        const float inset = parkedInsetFor(*this);
        const float lowest = juce::jmax(centre, (float)b.getBottom() - inset);
        return juce::Point<float>{(float)b.getX() + inset, juce::jmin(centre + step, lowest)};
    }
    return std::nullopt;
}

// The channel whose parked dot lies under card-local `local`, if any. The dot is 7 px across; the pickup reaches a few
// px past it, more on a zoomed-out canvas, as the knob's own does.
std::optional<int> ModuleComponent::parkedDotChannelAt(juce::Point<int> local) const {
    const float scale = hitToleranceScale();
    const float reach = kKnobLandingDotDiameter * 0.5f * scale + 4.0f * scale;
    const auto origin = getPosition().toFloat();
    for (const auto& cable : owner.buildVisibleCables())
        if (cable.parkedEnd && cable.destNodeId == nodeId.uid &&
            (cable.p2 - origin).getDistanceFrom(local.toFloat()) <= reach)
            return cable.destChannel;
    return std::nullopt;
}

// A press on a parked end's dot picks the cable up by its input jack, as the dot on a knob does. False when the press
// is not on one.
bool ModuleComponent::pickUpParkedDot(const juce::MouseEvent& e) {
    const auto parked = parkedDotChannelAt(e.getPosition());
    auto* mb = dynamic_cast<ModuleBase*>(module);
    if (!parked.has_value() || mb == nullptr)
        return false;
    owner.beginConnectionDrag(this, mb->mapInputChannel(*parked).visibleJackIndex, /*isInput*/ true, /*isMidi*/ false,
                              e.getScreenPosition());
    return true;
}
