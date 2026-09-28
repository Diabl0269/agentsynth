#pragma once

#include "MixerPanelComponent.h"
#include "UI/Layout/BottomDockComponent.h"
#include "UI/Layout/FocusRegion.h"
#include <functional>

// MixerFocusRegion.h -- registers the "mixer" focus region against `dock`'s panel, factored out of
// MainComponent::registerFocusRegions() so a detached mixer window can register the SAME
// region-registration logic against its OWN FocusRegionRegistry with a different `dockOpen`
// predicate, rather than re-deriving it. Any second registration site should thread through this
// helper too -- see FocusRegion.h's own header comment, which names that need.
namespace synth::ui {

/** Open only when both `dockOpen()` (the dock's own open/closed state -- MainComponent's
 *  isBottomDockVisible today, always-true for a future undockable window with no closed state of
 *  its own) AND `dock`'s Mixer tab is the one active -- the docked Timeline/Mixer tabs share one
 *  root, so `dockOpen()` alone is no longer enough to say the mixer region is open (the dock can
 *  be open on the OTHER tab). No `open` callback: no direct-focus shortcut targets the mixer today
 *  (same reason "modMatrix" has none), and Tab-cycling never opens a closed region regardless
 *  (FocusRegionRegistry::cycleFocus) -- see MixerFocusRegionTests.cpp.
 *
 *  A null `dockOpen` means "always open", matching FocusRegion::isOpen's own null-means-always-open
 *  contract, rather than crashing on an empty std::function call. */
inline void registerMixerFocusRegion(FocusRegionRegistry& registry, BottomDockComponent& dock,
                                     std::function<bool()> dockOpen) {
    registry.addRegion(
        {"mixer", &dock.getMixerPanel(),
         [&dock, dockOpen = std::move(dockOpen)] { return (!dockOpen || dockOpen()) && dock.isMixerTabActive(); },
         nullptr});
}

} // namespace synth::ui
