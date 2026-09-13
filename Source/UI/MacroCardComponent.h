#pragma once

#include "../AppUndoManager.h"
#include "../MacroSet.h"
#include <functional>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

class GraphEditor; // Forward declaration

/** The collapsed on-canvas representation of a synth::Macro (P8-12) — "a macro reads as one
 *  card on the canvas". One instance per collapsed macro, owned by GraphEditor's
 *  GraphContentComponent exactly like a ModuleComponent, and only ever visible while its macro
 *  is collapsed (GraphEditor::syncMacroCards hides it otherwise).
 *
 *  Deliberately NOT a ModuleComponent: a macro has no processor, no ports, no jacks — it is
 *  pure presentation over a GraphEditor::MacroSet entry. Body-drag mirrors ModuleComponent's own
 *  (ComponentDragger + GraphEditor::beginSelectionDrag/dragSelectionBy/finalizeSelectionDrag) so
 *  dragging the card moves every one of its (hidden) members via the exact same group-drag path
 *  a plain multi-select drag uses — see GraphEditor::beginMacroCardDrag.
 *
 *  Also a juce::TooltipClient (Fix 6/P8-12 follow-up): hovering a collapsed card shows its member
 *  module names, since the content preview drawn on the card is too small to read as text. */
class MacroCardComponent
    : public juce::Component
    , public juce::TooltipClient {
public:
    MacroCardComponent(GraphEditor& owner, juce::String macroId);
    ~MacroCardComponent() override;

    const juce::String& getMacroId() const noexcept { return macroId; }

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;

    /** Newline-separated member module names, capped so a huge macro doesn't produce an
     *  unreadable tooltip. */
    juce::String getTooltip() override;

    /** The card's own "N modules[, M ports]" line (founder-review fix G6, docs/macros.md §7
     *  item 4 note) — a MODULE count, excluding port nodes, with the port count named alongside
     *  it (never silently dropped) whenever the macro actually has one. Public so a test can pin
     *  the exact text against a founder-reported scenario (group 2 modules with a crossing cable
     *  -> 2 auto-created ports -> must read "2 modules", never "4 modules"), the same accessor
     *  pattern getTooltip() above already uses. Empty if `macroId` doesn't resolve. */
    juce::String getModuleCountText() const;

    /** Opens the inline rename editor over the card. Public so a test can drive it without
     *  synthesising a right-click + menu selection. */
    void beginRename();
    void finishRename(bool commit);
    bool isRenamingTitle() const noexcept { return nameEditor != nullptr; }

    /** Test accessors for the private layout functions below — so a test can assert the title
     *  row (and its double-click rename zone) never overlaps a bypass/mute badge slot, without
     *  duplicating either rectangle's arithmetic (P8-15d, T142). */
    juce::Rectangle<float> getToggleBadgeBoundsForTest(bool mute) const { return getToggleBadgeBounds(mute); }
    juce::Rectangle<int> getTitleRowBoundsForTest() const { return getTitleRowBounds(); }

    /** Replaces what a real right-click does with the menu showContextMenu() built — in place of
     *  the real showMenuAsync() (which opens a real popup and, on a headless Linux CI runner with
     *  no display, segfaults inside juce::PopupMenu::HelperClasses::MenuWindow — the exact issue
     *  ModuleComponent::setShowContextMenuHookForTest's own comment documents). A null hook
     *  restores the real behaviour rather than leaving the seam disarmed. A test installs a
     *  capturing hook to inspect the menu the real mouseDown() gesture actually built — including
     *  the T138 addCandidateSelection it was passed — without ever opening a popup. */
    void setShowContextMenuHookForTest(std::function<void(juce::PopupMenu&)> hook) {
        showContextMenuHook_ =
            hook ? std::move(hook) : [](juce::PopupMenu& m) { m.showMenuAsync(juce::PopupMenu::Options()); };
    }

    /** T165 live preview of ONE port's jack colour on this collapsed card, shown in real time while
     *  the Configure I/O picker is open without waiting for the pick to commit. View-layer ONLY — it
     *  is never written to the stored `synth::MacroPort::colour`, so a live preview pushes no undo
     *  step and dirties no data (the property T152's "commit once on close" needs to avoid a
     *  `recordGraphAndMacroChange` entry per pixel of slider movement). Keyed by the port's `nodeUuid`
     *  because a card draws all its macro's ports at once; the single open picker previews one of them,
     *  so `set`/`clear` carry a `nodeUuid`. Armed by `GraphEditor::previewMacroPortColour` on every
     *  picker tick, cleared by `GraphEditor::clearMacroPortColourPreview` when the pick commits or the
     *  picker closes. Unarmed by default. */
    void setPortColourPreview(const juce::String& nodeUuid, juce::Colour c);
    void clearPortColourPreview(const juce::String& nodeUuid);

    // Test seams: is `nodeUuid`'s jack preview armed, and what colour will paint() use for it
    // (mirrors paint()'s preview-first branch so a headless test can assert "the card tracks the
    // live pick" without capturing pixels).
    bool hasPortColourPreviewForTest(const juce::String& nodeUuid) const;
    juce::Colour resolvePortJackColourForTest(const juce::String& nodeUuid, const std::optional<juce::Colour>& stored,
                                              juce::Colour kindTint) const;

private:
    /** `priorSelection` (T138): whatever was selected right before mouseDown's own reselect —
     *  see GraphEditor::buildMacroMenu's addCandidateSelection comment for why this must be
     *  captured by the caller rather than read fresh in here. */
    void showContextMenu(const std::vector<juce::AudioProcessorGraph::NodeID>& priorSelection);

    // Set in the constructor to `[](juce::PopupMenu& m) { m.showMenuAsync(...); }`; a test replaces
    // it via setShowContextMenuHookForTest() — see that setter's comment.
    std::function<void(juce::PopupMenu&)> showContextMenuHook_;

    /** Top-right hit zone for the visible expand chevron — right-click's "Expand" menu item did
     *  the same thing but nothing on the card *looked* clickable, so grouping fresh modules and
     *  then finding your way back into them was a guessing game (double-click, undocumented). */
    juce::Rectangle<float> getExpandButtonBounds() const;

    /** One bypass/mute indeterminate-indicator badge's bounds (P8-15d, T142, docs/macros.md
     *  §5.6), just left of the expand chevron — `mute=false` is the outer (bypass) slot, `true`
     *  the inner (mute) slot nearer the chevron. Purely a function of `getExpandButtonBounds()`,
     *  so paint() and getTitleRowBounds() (which reserves room for both slots so a long macro
     *  name can never paint under them) read the SAME rectangles a test can assert against —
     *  the same "one layout definition" principle macroCardPortLayout applies to port jacks. */
    juce::Rectangle<float> getToggleBadgeBounds(bool mute) const;

    // Shared by getToggleBadgeBounds() and getTitleRowBounds() so the badge size/spacing can
    // never drift between where a badge is drawn and how much of the title row is reserved for it.
    static constexpr float kToggleBadgeSize = 8.0f;
    static constexpr float kToggleBadgeGap = 5.0f;

    /** The title row's hit zone, in this card's local bounds — the SAME rectangle paint() draws
     *  the name into (minus the chevron reservation), so a double-click's rename zone can never
     *  drift from what is drawn. paint() and mouseDoubleClick() both call this rather than
     *  computing the rect separately. */
    juce::Rectangle<int> getTitleRowBounds() const;

    GraphEditor& owner;
    juce::String macroId;

    juce::ComponentDragger dragger;
    juce::Point<int> dragStartPosition;
    bool bodyDragActive = false;

    std::unique_ptr<juce::TextEditor> nameEditor;

    // T165 live jack-colour preview for the single open picker's port. A pair (nodeUuid -> colour)
    // rather than a map because only one picker (one port) is ever open at a time, so a single
    // optional entry is correct and needs no container/hash. view-layer only (see setPortColourPreview).
    std::optional<std::pair<juce::String, juce::Colour>> portColourPreview_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MacroCardComponent)
};
