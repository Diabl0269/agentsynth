// Concern: MixerPanelComponent's shared section layout -- relaying layout changes to every column
// and the rail, growing the host while a divider drag needs room, and persisting the sections.
#include "MixerPanelComponent.h"

#include "UI/Mixer/MixerColumnComponent.h"

namespace synth::ui {

void MixerPanelComponent::wireSectionLayout() {
    sectionLayout_.onGeometryChanged = [this] { onSectionGeometryChanged(); };
    sectionLayout_.onAppearanceChanged = [this] { onSectionAppearanceChanged(); };
    sectionLayout_.onCommitted = [this] { onSectionLayoutCommitted(); };
}

// Loaded once per store: the heights are app-wide, like the bottom dock's own height, so the docked
// panel and the "both places" mirror read the same keys.
void MixerPanelComponent::setSettingsStore(juce::PropertiesFile* settings) {
    settings_ = settings;
    if (settings_ != nullptr)
        sectionLayout_.loadFrom(*settings_);
}

bool MixerPanelComponent::contentScrollsVertically() const {
    const bool hostCanGrow = canGrowHost && canGrowHost();
    return !hostCanGrow && sectionLayout_.requiredColumnHeight() > getHeight();
}

// A divider drag takes space from the fader first; once the fader would drop under its minimum, the
// drag keeps going by asking the host (the bottom dock, or the Own panel) for the difference. The
// host clamps to its own limit, so the next step re-measures rather than assuming the growth
// happened. Shrinking never shrinks the host: the space goes back to the fader. A host that cannot
// grow at all (a detached window) is handled by resized() scrolling the content instead.
void MixerPanelComponent::onSectionGeometryChanged() {
    if (sectionLayout_.getDraggingDivider() >= 0 && canGrowHost && canGrowHost() && growHost) {
        const int shortfall = sectionLayout_.requiredColumnHeight() - getHeight();
        if (shortfall > 0) {
            grewHostThisDrag_ = true;
            growHost(shortfall, false);
        }
    }
    resized();
}

void MixerPanelComponent::onSectionAppearanceChanged() {
    for (auto& column : stripColumns_)
        column->repaintSectionDividers();
    if (masterColumn_ != nullptr)
        masterColumn_->repaintSectionDividers();
    rail_.repaint();
}

// Persists on gesture end only (drag release, show/hide, double-click reset), never per drag step --
// the same split the bottom dock's own resize handle uses.
void MixerPanelComponent::onSectionLayoutCommitted() {
    if (grewHostThisDrag_) {
        grewHostThisDrag_ = false;
        if (growHost)
            growHost(0, true);
    }
    rail_.refreshLayout();
    if (settings_ == nullptr)
        return;
    sectionLayout_.saveTo(*settings_);
    settings_->saveIfNeeded();
}

} // namespace synth::ui
