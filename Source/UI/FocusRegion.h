#pragma once

#include "Theme/AppLookAndFeel.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

// T159: the app-wide keyboard focus-region framework — Tab/Shift+Tab cycling plus two direct-focus
// shortcuts, phase 1 of a 3-part epic (T160 adds arrow-key navigation WITHIN the module library,
// T161 within the timeline track headers; neither is built here). See docs/shortcuts.md and
// docs/architecture.md §8 for the user-facing behaviour this implements.
//
// Deliberately a plain, ownable type rather than a Desktop-global singleton (Source/Plugin/CLAUDE.md
// forbids that shape for exactly this reason: a host process can run multiple plugin instances, and
// a future separate-window mixer/timeline would need its OWN registry rather than sharing one across
// every top-level window in the process). `MainComponent` owns the one instance that exists today, as
// a plain member (`focusRegions_`), and wires it to the getters/toggles it already has — this type
// itself knows nothing about MainComponent.
namespace synth::ui {

// One region MainComponent's Tab-cycle and direct-focus shortcuts can land keyboard focus in.
//
//  - `id`   — a short stable string ("library", "canvas", "timeline", "aiPanel", "modMatrix" today).
//             Never persisted, so it is free to add/rename as T160/T161 land.
//  - `root` — the component `grabKeyboardFocus()` is called on. Never null once registered.
//  - `isOpen` — null means "always open" (the graph canvas, which has no closed state); otherwise
//             called on demand, never cached, so it always reflects live panel-visibility state.
//  - `open` — how to open this region if `focusRegionById` is asked to focus it while closed. Null
//             for a region with no closed state (`isOpen` also null) or one no direct-focus shortcut
//             ever targets (Mod Matrix, per T159's scope — Tab-cycling never opens a closed region,
//             see FocusRegionRegistry::cycleFocus, so leaving this null there is safe either way).
struct FocusRegion {
    juce::String id;
    juce::Component* root = nullptr;
    std::function<bool()> isOpen;
    std::function<void()> open;

    bool isCurrentlyOpen() const { return !isOpen || isOpen(); }
};

// Registration + the two cycle/direct-focus operations, all built on top of two small PURE
// functions (`regionContaining`, `nextOpenRegionId`) that never touch real OS keyboard focus — that
// split is what makes the region-choice LOGIC headlessly testable (see FocusRegionTests.cpp) even
// though a real `grabKeyboardFocus()` needs a native peer this test suite has never created (the
// same constraint FocusArbitrationTests.cpp's SurfaceResolverRealFocus documents).
class FocusRegionRegistry {
public:
    void addRegion(FocusRegion region) { regions_.push_back(std::move(region)); }
    void clear() { regions_.clear(); }
    const std::vector<FocusRegion>& getRegions() const { return regions_; }

    const FocusRegion* findById(const juce::String& id) const {
        for (const auto& r : regions_)
            if (r.id == id)
                return &r;
        return nullptr;
    }

    // The region `component` sits inside — its root IS `component`, or its root `isParentOf` it.
    // Pure component-TREE logic (juce::Component::isParentOf needs no native peer/real focus), so
    // this is exactly as testable with a real MainComponent tree as it is in production.
    //
    // Regions can NEST (Mod Matrix is a child component of the Graph Canvas), so more than one
    // region can match: prefer the most specific one (the one whose root is deepest / itself
    // contained by the other candidate's root), never just the first-registered match, or focus
    // inside the Mod Matrix would always be reported as "canvas".
    const FocusRegion* regionContaining(juce::Component* component) const {
        if (component == nullptr)
            return nullptr;
        const FocusRegion* best = nullptr;
        for (const auto& r : regions_) {
            if (r.root == nullptr || (r.root != component && !r.root->isParentOf(component)))
                continue;
            if (best == nullptr || best->root->isParentOf(r.root))
                best = &r;
        }
        return best;
    }

    std::vector<const FocusRegion*> openRegions() const {
        std::vector<const FocusRegion*> result;
        for (const auto& r : regions_)
            if (r.isCurrentlyOpen())
                result.push_back(&r);
        return result;
    }

    // Pure decision: given the id currently holding focus (empty/unknown counts as "no current
    // region"), the id of the next OPEN region Tab-cycling should land in. Closed regions are
    // skipped entirely — a LOCKED T159 decision, see docs/shortcuts.md — and the ends wrap. Every
    // region closed (never happens today; Canvas has no closed state) answers an empty string.
    juce::String nextOpenRegionId(const juce::String& currentId, bool forward) const {
        const auto open = openRegions();
        if (open.empty())
            return {};
        int idx = -1;
        for (size_t i = 0; i < open.size(); ++i) {
            if (open[i]->id == currentId) {
                idx = (int)i;
                break;
            }
        }
        const int count = (int)open.size();
        int next;
        if (idx < 0)
            next = forward ? 0 : count - 1;
        else
            next = forward ? (idx + 1) % count : (idx - 1 + count) % count;
        return open[(size_t)next]->id;
    }

    // Focuses `id`'s root, opening it first via `open` if it is currently closed — the direct-focus
    // shortcuts' contract (Cmd+Shift+T/L), deliberately different from cycleFocus below, which never
    // opens anything. Returns false only if `id` is unknown or its root is null (never happens once
    // MainComponent has registered every region, but keeps this safe to call speculatively).
    bool focusRegionById(const juce::String& id) {
        for (auto& r : regions_) {
            if (r.id != id)
                continue;
            if (r.root == nullptr)
                return false;
            if (!r.isCurrentlyOpen() && r.open)
                r.open();
            r.root->grabKeyboardFocus();
            return true;
        }
        return false;
    }

    // Production Tab/Shift+Tab entry point: finds the OPEN region containing the REAL currently-
    // focused component, moves to the next/prev OPEN region (wrapping), and grabs its keyboard
    // focus. Never opens a closed region — only the two open PURE functions above decide WHICH
    // region that is, so a test can pin the decision without a real focus grab ever happening.
    bool cycleFocus(bool forward) {
        auto* focused = juce::Component::getCurrentlyFocusedComponent();
        const auto* current = regionContaining(focused);
        const auto currentId = current != nullptr ? current->id : juce::String();
        const auto nextId = nextOpenRegionId(currentId, forward);
        if (nextId.isEmpty())
            return false;
        return focusRegionById(nextId);
    }

private:
    std::vector<FocusRegion> regions_;
};

// The theme's one "selected/focused" token (docs/theming.md's `accent`), painted as a solid ~2px
// outline around `comp`'s own bounds whenever it or a descendant holds keyboard focus
// (`hasKeyboardFocus(true)`) — the visual half of T159, shared by every focus-region root's paint()
// rather than each one reinventing it. Mirrors the "accent when focused" treatment
// AppLookAndFeel already applies to ComboBox/TextEditor outlines. Event-driven, not a per-tick
// timer — but `Component::focusGained`/`focusLost`/`focusOfChildComponentChanged` are no-op virtuals
// by default, so nothing repaints a region root just because focus moved into or out of it. The
// repaint trigger lives in MainComponent, which is a `juce::FocusChangeListener` and repaints every
// registered region root on `globalFocusChanged` (see MainComponent::globalFocusChanged) — this
// function only decides WHAT to paint once a repaint happens, not when one happens.
inline void paintFocusRegionOutline(juce::Component& comp, juce::Graphics& g) {
    if (!comp.hasKeyboardFocus(true))
        return;
    juce::Colour colour = juce::Colours::orange;
    if (auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&comp.getLookAndFeel()))
        colour = lf->getTheme().colors.accent;
    g.setColour(colour);
    g.drawRect(comp.getLocalBounds(), 2);
}

} // namespace synth::ui
