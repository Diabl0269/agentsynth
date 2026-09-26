// Concern: FRO270 (docs/control/midi-remote-ui.md#surface-centre) -- a press-drag over EMPTY grid
// space (a press that lands on a cell never reaches here: ControllerSurfaceCell is a real
// juce::Component covering its own bounds and handles its own mouseDown, so this component's own
// mouseDown only ever fires for a click that missed every cell). Debounces click-vs-drag the same
// way ControllerSurfaceCell itself does (isDragging_/dragStartMouse_): a plain click with no
// movement clears the selection; a real drag paints a marquee and, on release, selects whichever
// cells it intersects.

#include "ControllerSurfaceComponent.h"

#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

#include <algorithm>

namespace synth::ui {

std::vector<juce::String> ControllerSurfaceComponent::collectMarqueeHits() const {
    std::vector<juce::String> hits;
    for (const auto* cell : cells_)
        if (cell->getBounds().intersects(marqueeRect_))
            hits.push_back(cell->getControlId());
    return hits;
}

void ControllerSurfaceComponent::mouseDown(const juce::MouseEvent& event) {
    marqueeAnchor_ = event.getPosition();
    marqueeRect_ = {};
    marqueeActive_ = false;
    marqueeAdditive_ = event.mods.isShiftDown();
    grabKeyboardFocus(); // so Esc/Delete reach keyPressed() even for a plain click that clears
}

void ControllerSurfaceComponent::mouseDrag(const juce::MouseEvent& event) {
    const auto previousRect = marqueeRect_;
    marqueeRect_ = juce::Rectangle<int>(marqueeAnchor_, event.getPosition());
    marqueeActive_ = true;
    // Only the changed region -- the union of where the marquee WAS and where it is now, expanded
    // by the border's stroke width (Source/UI/CLAUDE.md's repaint rule: never repaint the whole
    // surface for a drag that only moved a few pixels).
    repaint(previousRect.getUnion(marqueeRect_).expanded(2));
}

void ControllerSurfaceComponent::mouseUp(const juce::MouseEvent&) {
    if (!marqueeActive_) {
        // A plain click that missed every cell and never dragged -- clears the selection.
        if (!selectedIds_.empty())
            setSelectionInternal({});
        return;
    }

    auto hits = collectMarqueeHits();
    if (marqueeAdditive_) {
        std::vector<juce::String> merged = selectedIds_;
        for (auto& id : hits)
            if (std::find(merged.begin(), merged.end(), id) == merged.end())
                merged.push_back(std::move(id));
        setSelectionInternal(std::move(merged));
    } else {
        setSelectionInternal(std::move(hits));
    }

    const auto eraseRect = marqueeRect_;
    marqueeActive_ = false;
    marqueeRect_ = {};
    repaint(eraseRect.expanded(2));
}

void ControllerSurfaceComponent::paintOverChildren(juce::Graphics& g) {
    if (!marqueeActive_)
        return;
    auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());
    const juce::Colour accent = lf != nullptr ? lf->getTheme().colors.accent : juce::Colours::cyan;
    g.setColour(accent.withAlpha(0.12f));
    g.fillRect(marqueeRect_);
    g.setColour(accent);
    g.drawRect(marqueeRect_, 1);
}

} // namespace synth::ui
