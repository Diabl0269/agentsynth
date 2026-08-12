#pragma once

#include "LayoutUtil.h"
#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>

namespace synth::ui {

/**
 * @brief The in-app clipboard behind Copy / Paste / Duplicate for a group of modules.
 *
 * The payload is **snippet JSON** — the exact dialect `SnippetManager::extractSnippet` produces and
 * `insertSnippet` consumes. Reusing that instead of inventing a second copy format is what makes a
 * paste's wiring correct for free: a snippet keeps only connections whose *both* endpoints are
 * inside the copied group, stores modulation as intent rather than as attenuverter nodes, and is
 * renumbered to a fresh id range on insert. The copies therefore wire up to each other and never
 * back to the originals, and no wire crosses the group boundary in either direction.
 *
 * Pure state — no graph, no Component, no filesystem — so the paste-position rules are testable
 * headlessly. `GraphEditor` owns one and is the only thing that turns it into nodes.
 */
class ModuleClipboard {
public:
    /** Distance each successive paste (and every duplicate) is offset by, in canvas pixels.
     *  A multiple of the layout grid so an offset copy still lands on-grid, and large enough that
     *  the copy reads as its own card rather than a shadow of what it came from. */
    static constexpr int kOffsetStep = 5 * LayoutUtil::kGridSize; // 40 px

    /** Stores a payload copied from a group whose top-left canvas corner was `origin`.
     *  Resets the cascade, so the first paste afterwards lands exactly one step off the original. */
    void set(const juce::var& snippet, juce::Point<int> origin) {
        payload = snippet;
        anchor = origin;
        cascade = 0;
    }

    void clear() {
        payload = juce::var();
        anchor = {};
        cascade = 0;
    }

    /** A clipboard with no payload — or one carrying zero placeable nodes — has nothing to paste.
     *  The zero-node case is real: copying a selection of nothing but graph I/O nodes yields a
     *  well-formed snippet with an empty `nodes` array. */
    bool isEmpty() const { return getModuleCount() <= 0; }

    int getModuleCount() const {
        auto* obj = payload.getDynamicObject();
        if (obj == nullptr || !obj->hasProperty("nodes"))
            return 0;
        auto* nodes = obj->getProperty("nodes").getArray();
        return nodes != nullptr ? nodes->size() : 0;
    }

    const juce::var& getPayload() const { return payload; }

    /** Canvas position the next unpositioned paste should land at.
     *
     *  Advances one step down-right each call so repeated Cmd+V builds a visible cascade instead of
     *  stacking every copy on the same pixel — which would look like nothing happened after the
     *  first paste. Only call this when a paste is actually going ahead: it mutates the cascade. */
    juce::Point<int> nextPastePosition() {
        ++cascade;
        return anchor + juce::Point<int>(kOffsetStep * cascade, kOffsetStep * cascade);
    }

    /** Re-anchors the cascade at an explicitly chosen drop point (the canvas "Paste Here" menu),
     *  so a following keyboard paste continues from where the user last put one. */
    void anchorAt(juce::Point<int> pos) {
        anchor = pos;
        cascade = 0;
    }

    juce::Point<int> getAnchor() const noexcept { return anchor; }
    int getCascadeCount() const noexcept { return cascade; }

private:
    juce::var payload;
    juce::Point<int> anchor;
    int cascade = 0;
};

} // namespace synth::ui
