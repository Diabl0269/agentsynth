#pragma once

#include "MixerPanelComponent.h"
#include "UI/Layout/BottomDockComponent.h"
#include "UI/Layout/FocusRegion.h"
#include <functional>

// MixerFocusRegion.h -- FRO18 plan (a)'s "FRO12 seam": registers the "mixer" T159 focus region
// against `dock`'s panel, factored out of MainComponent::registerFocusRegions() specifically so a
// future detached mixer window (P9-6/FRO12) can register the SAME region-registration logic
// against its OWN FocusRegionRegistry with a different `dockOpen` predicate, rather than
// re-deriving it. Whichever of FRO12/FRO18 lands second re-threads through this helper (plan's own
// Risks (g)) -- see FocusRegion.h's own header comment, which already names this future need.
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
